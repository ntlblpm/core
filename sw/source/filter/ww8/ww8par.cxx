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

#include <sal/config.h>
#include <sal/log.hxx>

#include <com/sun/star/embed/Aspects.hpp>
#include <com/sun/star/embed/ElementModes.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/packages/XPackageEncryption.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/text/XTextFieldsSupplier.hpp>

#include <i18nlangtag/languagetag.hxx>

#include <comphelper/configuration.hxx>
#include <comphelper/string.hxx>
#include <unotools/ucbstreamhelper.hxx>
#include <unotools/streamwrap.hxx>
#include <rtl/random.h>
#include <rtl/ustring.hxx>
#include <rtl/ustrbuf.hxx>

#include <sfx2/docinf.hxx>
#include <sfx2/frame.hxx>
#include <sfx2/zoomitem.hxx>
#include <tools/urlobj.hxx>
#include <unotools/tempfile.hxx>

#include <comphelper/docpasswordrequest.hxx>
#include <comphelper/documentinfo.hxx>
#include <comphelper/propertysequence.hxx>

#include <editeng/outlobj.hxx>
#include <editeng/brushitem.hxx>
#include <editeng/formatbreakitem.hxx>
#include <editeng/tstpitem.hxx>
#include <editeng/ulspitem.hxx>
#include <editeng/langitem.hxx>
#include <editeng/opaqitem.hxx>
#include <editeng/charhiddenitem.hxx>
#include <editeng/fontitem.hxx>
#include <editeng/editeng.hxx>
#include <svx/svdoole2.hxx>
#include <svx/svdoashp.hxx>
#include <svx/svxerr.hxx>
#include <filter/msfilter/mscodec.hxx>
#include <svx/svdmodel.hxx>
#include <svx/xflclit.hxx>
#include <svx/sdasitm.hxx>
#include <svx/sdtagitm.hxx>
#include <svx/sdtcfitm.hxx>
#include <svx/sdtditm.hxx>
#include <svx/sdtmfitm.hxx>
#include <fmtfld.hxx>
#include <fmturl.hxx>
#include <fmtinfmt.hxx>
#include <reffld.hxx>
#include <fmthdft.hxx>
#include <fmtcntnt.hxx>
#include <fmtcnct.hxx>
#include <fmtanchr.hxx>
#include <fmtpdsc.hxx>
#include <ftninfo.hxx>
#include <fmtftn.hxx>
#include <txtftn.hxx>
#include <ndtxt.hxx>
#include <pagedesc.hxx>
#include <paratr.hxx>
#include <poolfmt.hxx>
#include <fmtclbl.hxx>
#include <section.hxx>
#include <docsh.hxx>
#include <IDocumentFieldsAccess.hxx>
#include <IDocumentLayoutAccess.hxx>
#include <IDocumentMarkAccess.hxx>
#include <IDocumentStylePoolAccess.hxx>
#include <IDocumentExternalData.hxx>
#include <../../core/inc/DocumentRedlineManager.hxx>
#include <docufld.hxx>
#include <swfltopt.hxx>
#include <utility>
#include <viewsh.hxx>
#include <shellres.hxx>
#include <swerror.h>
#include <swtable.hxx>
#include <fchrfmt.hxx>
#include <charfmt.hxx>
#include <fmtautofmt.hxx>
#include <IDocumentSettingAccess.hxx>
#include "sprmids.hxx"

#include "writerwordglue.hxx"

#include <ndgrf.hxx>
#include <editeng/editids.hrc>
#include <fmtflcnt.hxx>
#include <txatbase.hxx>

#include "ww8par2.hxx"

#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/document/XDocumentPropertiesSupplier.hpp>
#include <com/sun/star/document/XViewDataSupplier.hpp>

#include <svl/lngmisc.hxx>
#include <svl/itemiter.hxx>
#include <svl/whiter.hxx>

#include <comphelper/indexedpropertyvalues.hxx>
#include <comphelper/processfactory.hxx>
#include <basic/basmgr.hxx>

#include "ww8toolbar.hxx"
#include <o3tl/unit_conversion.hxx>
#include <o3tl/safeint.hxx>
#include <osl/file.hxx>

#include <breakit.hxx>

#include <sfx2/docfile.hxx>
#include <swdll.hxx>
#include "WW8Sttbf.hxx"
#include "WW8FibData.hxx"
#include <unordered_set>
#include <memory>

#include <com/sun/star/i18n/XBreakIterator.hpp>
#include <com/sun/star/i18n/ScriptType.hpp>
#include <unotools/pathoptions.hxx>
#include <com/sun/star/ucb/SimpleFileAccess.hpp>

#include <com/sun/star/script/vba/XVBACompatibility.hpp>
#include <comphelper/sequenceashashmap.hxx>
#include <oox/ole/vbaproject.hxx>
#include <oox/ole/olestorage.hxx>
#include <comphelper/storagehelper.hxx>
#include <sfx2/DocumentMetadataAccess.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <officecfg/Office/Common.hxx>
#include <unotxdoc.hxx>

using namespace ::com::sun::star;
using namespace sw::util;
using namespace sw::types;
using namespace nsHdFtFlags;

static SwMacroInfo* GetMacroInfo( SdrObject* pObj )
{
    if ( pObj )
    {
        sal_uInt16 nCount = pObj->GetUserDataCount();
        for( sal_uInt16 i = 0; i < nCount; i++ )
        {
            SdrObjUserData* pData = pObj->GetUserData( i );
            if( pData && pData->GetInventor() == SdrInventor::ScOrSwDraw
                && pData->GetId() == SW_UD_IMAPDATA)
            {
                return dynamic_cast<SwMacroInfo*>(pData);
            }
        }
        SwMacroInfo* pData = new SwMacroInfo;
        pObj->AppendUserData(std::unique_ptr<SdrObjUserData>(pData));
        return pData;
    }

    return nullptr;
};

static void lclGetAbsPath(OUString& rPath, sal_uInt16 nLevel, SwDocShell const * pDocShell)
{
    OUStringBuffer aTmpStr;
    while( nLevel )
    {
        aTmpStr.append("../");
        --nLevel;
    }
    if (!aTmpStr.isEmpty())
        aTmpStr.append(rPath);
    else
        aTmpStr = rPath;

    if (!aTmpStr.isEmpty())
    {
        bool bWasAbs = false;
        rPath = pDocShell->GetMedium()->GetURLObject().smartRel2Abs( aTmpStr.makeStringAndClear(), bWasAbs ).GetMainURL( INetURLObject::DecodeMechanism::NONE );
        // full path as stored in SvxURLField must be encoded
    }
}

namespace
{
    void lclIgnoreUString32(SvStream& rStrm)
    {
        sal_uInt32 nChars(0);
        rStrm.ReadUInt32(nChars);
        nChars *= 2;
        rStrm.SeekRel(nChars);
    }
}

// returns true if an embedded null was found
static bool clipToFirstNull(OUString& rStr)
{
    sal_Int32 nEmbeddedNullIdx = rStr.indexOf(0);
    if (nEmbeddedNullIdx != -1)
        rStr = rStr.copy(0, nEmbeddedNullIdx);
    return nEmbeddedNullIdx != -1;
}

void SwWW8ImplReader::ReadEmbeddedData(SvStream& rStrm, SwDocShell const * pDocShell, struct HyperLinksTable& hlStr)
{
    // (0x01B8) HLINK
    // const sal_uInt16 WW8_ID_HLINK               = 0x01B8;
    constexpr sal_uInt32 WW8_HLINK_BODY             = 0x00000001;   /// Contains file link or URL.
    constexpr sal_uInt32 WW8_HLINK_ABS              = 0x00000002;   /// Absolute path.
    constexpr sal_uInt32 WW8_HLINK_DESCR            = 0x00000014;   /// Description.
    constexpr sal_uInt32 WW8_HLINK_MARK             = 0x00000008;   /// Text mark.
    constexpr sal_uInt32 WW8_HLINK_FRAME            = 0x00000080;   /// Target frame.
    constexpr sal_uInt32 WW8_HLINK_UNC              = 0x00000100;   /// UNC path.

    //sal_uInt8 maGuidStdLink[ 16 ] ={
    //    0xD0, 0xC9, 0xEA, 0x79, 0xF9, 0xBA, 0xCE, 0x11, 0x8C, 0x82, 0x00, 0xAA, 0x00, 0x4B, 0xA9, 0x0B };

    sal_uInt8 const aGuidUrlMoniker[ 16 ] = {
        0xE0, 0xC9, 0xEA, 0x79, 0xF9, 0xBA, 0xCE, 0x11, 0x8C, 0x82, 0x00, 0xAA, 0x00, 0x4B, 0xA9, 0x0B };

    sal_uInt8 const aGuidFileMoniker[ 16 ] = {
        0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 };

    sal_uInt8 aGuid[16];
    sal_uInt32 nFlags(0);

    rStrm.ReadBytes(aGuid, 16);
    rStrm.SeekRel( 4 );
    rStrm.ReadUInt32( nFlags );

    std::unique_ptr< OUString > xLongName;    // link / file name
    std::unique_ptr< OUString > xShortName;   // 8.3-representation of file name
    std::unique_ptr< OUString > xTextMark;    // text mark

    // description (ignore)
    if( ::get_flag( nFlags, WW8_HLINK_DESCR ) )
        lclIgnoreUString32( rStrm );

    // target frame
    if( ::get_flag( nFlags, WW8_HLINK_FRAME ) )
    {
        hlStr.tarFrame = read_uInt32_lenPrefixed_uInt16s_ToOUString(rStrm);
    }

        // UNC path
    if( ::get_flag( nFlags, WW8_HLINK_UNC ) )
    {
        // MS-OSHARED: An unsigned integer that specifies the number of Unicode characters in the
        // string field, including the null-terminating character.
        sal_uInt32 nStrLen(0);
        rStrm.ReadUInt32(nStrLen);
        if (nStrLen)
        {
            xLongName.reset(new OUString(read_uInt16s_ToOUString(rStrm, nStrLen - 1)));
            rStrm.SeekRel(sizeof(sal_Unicode)); // skip null-byte at end
            lclGetAbsPath( *xLongName, 0 , pDocShell);
        }
    }
    // file link or URL
    else if( ::get_flag( nFlags, WW8_HLINK_BODY ) )
    {
        rStrm.ReadBytes(aGuid, 16);

        if( memcmp(aGuid, aGuidFileMoniker, 16) == 0 )
        {
            sal_uInt16 nLevel = 0;                  // counter for level to climb down in path
            rStrm.ReadUInt16( nLevel );
            // MS-OSHARED: An unsigned integer that specifies the number of
            // ANSI characters in ansiPath, including the terminating NULL character
            sal_uInt32 nUnits = 0;
            rStrm.ReadUInt32(nUnits);
            if (!nUnits)
                xShortName.reset(new OUString);
            else
            {
                OString sStr(read_uInt8s_ToOString(rStrm, nUnits - 1));
                rStrm.SeekRel(sizeof(sal_uInt8)); // skip null-byte at end
                xShortName.reset(new OUString(sStr.getStr(), sStr.getLength(), GetCharSetFromLanguage()));
            }
            rStrm.SeekRel( 24 );

            sal_uInt32 nStrLen(0);
            rStrm.ReadUInt32( nStrLen );
            if( nStrLen )
            {
                nStrLen = 0;
                rStrm.ReadUInt32( nStrLen );
                nStrLen /= 2;
                rStrm.SeekRel( 2 );
                // MS-OSHARED: This array MUST not include a terminating NULL character.
                xLongName.reset(new OUString(read_uInt16s_ToOUString(rStrm, nStrLen)));
                lclGetAbsPath( *xLongName, nLevel, pDocShell);
            }
            else
                lclGetAbsPath( *xShortName, nLevel, pDocShell);
        }
        else if( memcmp(aGuid, aGuidUrlMoniker, 16) == 0 )
        {
            // MS-OSHARED: An unsigned integer that specifies the size of this
            // structure in bytes, excluding the size of the length field. The
            // value of this field MUST be ... the byte size of the url
            // field (including the terminating NULL character)
            sal_uInt32 nStrLen(0);
            rStrm.ReadUInt32( nStrLen );
            nStrLen /= 2;
            if (!nStrLen)
                xLongName.reset(new OUString);
            else
            {
                xLongName.reset(new OUString(read_uInt16s_ToOUString(rStrm, nStrLen - 1)));
                rStrm.SeekRel(sizeof(sal_Unicode)); // skip null-byte at end
            }
            if( !::get_flag( nFlags, WW8_HLINK_ABS ) )
                lclGetAbsPath( *xLongName, 0 ,pDocShell);
        }
        else
        {
            SAL_INFO("sw.ww8", "WW8Hyperlink::ReadEmbeddedData - unknown content GUID");
        }
    }

    // text mark
    if( ::get_flag( nFlags, WW8_HLINK_MARK ) )
    {
        xTextMark.reset(new OUString(read_uInt32_lenPrefixed_uInt16s_ToOUString(rStrm)));
        if (clipToFirstNull(*xTextMark))
            SAL_WARN("sw.ww8", "HLINK_MARK with embedded null, truncating to: " << *xTextMark);
    }

    if (!xLongName && xShortName)
        xLongName.reset(new OUString(*xShortName));
    else if (!xLongName && xTextMark)
        xLongName.reset( new OUString );

    if (xLongName)
    {
        if (clipToFirstNull(*xLongName))
            SAL_WARN("sw.ww8", "HLINK with embedded null, truncating to: " << *xLongName);
        if (xTextMark)
        {
            if (xLongName->isEmpty())
                *xTextMark = xTextMark->replace('!', '.');
            *xLongName += "#" + *xTextMark;
        }
        hlStr.hLinkAddr = *xLongName;
    }
}

namespace {

class BasicProjImportHelper
{
    SwDocShell& mrDocShell;
    uno::Reference< uno::XComponentContext > mxCtx;
public:
    explicit BasicProjImportHelper( SwDocShell& rShell ) : mrDocShell( rShell ),
      mxCtx(comphelper::getProcessComponentContext())
    {
    }
    bool import( const uno::Reference< io::XInputStream >& rxIn );
    OUString getProjectName() const;
};

}

bool BasicProjImportHelper::import( const uno::Reference< io::XInputStream >& rxIn )
{
    bool bRet = false;
    try
    {
        oox::ole::OleStorage root( mxCtx, rxIn, false );
        oox::StorageRef vbaStg = root.openSubStorage( u"Macros"_ustr , false );
        if ( vbaStg )
        {
            oox::ole::VbaProject aVbaPrj( mxCtx, mrDocShell.GetModel(), u"Writer" );
            bRet = aVbaPrj.importVbaProject( *vbaStg );
        }
    }
    catch( const uno::Exception& )
    {
        bRet = false;
    }
    return bRet;
}

OUString BasicProjImportHelper::getProjectName() const
{
    OUString sProjName( u"Standard"_ustr );
    uno::Reference< beans::XPropertySet > xProps( mrDocShell.GetModel(), uno::UNO_QUERY );
    if ( !xProps )
        return sProjName;
    try
    {
        uno::Reference< script::vba::XVBACompatibility > xVBA( xProps->getPropertyValue( u"BasicLibraries"_ustr ), uno::UNO_QUERY  );
        if ( !xVBA )
            return sProjName;
        sProjName = xVBA->getProjectName();

    }
    catch( const uno::Exception& )
    {
    }
    return sProjName;
}

namespace {

class Sttb : public TBBase
{
struct SBBItem
{
    sal_uInt16 cchData;
    OUString data;
    SBBItem() : cchData(0){}
};
    sal_uInt16 m_fExtend;
    sal_uInt16 m_cData;
    sal_uInt16 m_cbExtra;

    std::vector< SBBItem > m_dataItems;

    Sttb(Sttb const&) = delete;
    Sttb& operator=(Sttb const&) = delete;

public:
    Sttb();

    bool Read(SvStream &rS) override;
    OUString getStringAtIndex( sal_uInt32 );
};

}

Sttb::Sttb()
    : m_fExtend(0)
    , m_cData(0)
    , m_cbExtra(0)
{
}

bool Sttb::Read( SvStream& rS )
{
    SAL_INFO("sw.ww8", "stream pos " << rS.Tell());
    nOffSet = rS.Tell();
    rS.ReadUInt16( m_fExtend ).ReadUInt16( m_cData ).ReadUInt16( m_cbExtra );
    if ( m_cData )
    {
        //if they are all going to be empty strings, how many could there be
        const size_t nMaxPossibleRecords = rS.remainingSize() / sizeof(sal_uInt16);
        if (m_cData > nMaxPossibleRecords)
            return false;
        for ( sal_Int32 index = 0; index < m_cData; ++index )
        {
            SBBItem aItem;
            rS.ReadUInt16( aItem.cchData );
            aItem.data = read_uInt16s_ToOUString(rS, aItem.cchData);
            m_dataItems.push_back( aItem );
        }
    }
    return true;
}

OUString
Sttb::getStringAtIndex( sal_uInt32 index )
{
    OUString aRet;
    if ( index < m_dataItems.size() )
        aRet = m_dataItems[ index ].data;
    return aRet;

}

SwMSDffManager::SwMSDffManager( SwWW8ImplReader& rRdr, bool bSkipImages )
    : SvxMSDffManager(*rRdr.m_pTableStream, rRdr.GetBaseURL(), rRdr.m_xWwFib->m_fcDggInfo,
        rRdr.m_pDataStream, nullptr, 0, COL_WHITE, rRdr.m_pStrm, bSkipImages),
    m_rReader(rRdr), m_pFallbackStream(nullptr)
{
    SetSvxMSDffSettings( GetSvxMSDffSettings() );
    nSvxMSDffOLEConvFlags = SwMSDffManager::GetFilterFlags();
}

sal_uInt32 SwMSDffManager::GetFilterFlags()
{
    sal_uInt32 nFlags(0);
    if (officecfg::Office::Common::Filter::Microsoft::Import::MathTypeToMath::get())
        nFlags |= OLE_MATHTYPE_2_STARMATH;
    if (officecfg::Office::Common::Filter::Microsoft::Import::ExcelToCalc::get())
        nFlags |= OLE_EXCEL_2_STARCALC;
    if (officecfg::Office::Common::Filter::Microsoft::Import::PowerPointToImpress::get())
        nFlags |= OLE_POWERPOINT_2_STARIMPRESS;
    if (officecfg::Office::Common::Filter::Microsoft::Import::WinWordToWriter::get())
        nFlags |= OLE_WINWORD_2_STARWRITER;
    return nFlags;
}

/*
 * I would like to override the default OLE importing to add a test
 * and conversion of OCX controls from their native OLE type into our
 * native nonOLE Form Control Objects.
 */
// #i32596# - consider new parameter <_nCalledByGroup>
rtl::Reference<SdrObject> SwMSDffManager::ImportOLE( sal_uInt32 nOLEId,
                                      const Graphic& rGrf,
                                      const tools::Rectangle& rBoundRect,
                                      const tools::Rectangle& rVisArea,
                                      const int _nCalledByGroup ) const
{
    // #i32596# - no import of OLE object, if it's inside a group.
    // NOTE: This can be undone, if grouping of Writer fly frames is possible or
    // if drawing OLE objects are allowed in Writer.
    if ( _nCalledByGroup > 0 )
    {
        return nullptr;
    }

    rtl::Reference<SdrObject> pRet;
    OUString sStorageName;
    rtl::Reference<SotStorage> xSrcStg;
    uno::Reference < embed::XStorage > xDstStg;
    if( GetOLEStorageName( nOLEId, sStorageName, xSrcStg, xDstStg ))
    {
        rtl::Reference<SotStorage> xSrc = xSrcStg->OpenSotStorage(sStorageName);
        OSL_ENSURE(m_rReader.m_xFormImpl, "No Form Implementation!");
        css::uno::Reference< css::drawing::XShape > xShape;
        if ( (!(m_rReader.m_bIsHeader || m_rReader.m_bIsFooter)) &&
            m_rReader.m_xFormImpl->ReadOCXStream(xSrc,&xShape,true))
        {
            pRet = SdrObject::getSdrObjectFromXShape(xShape);
        }
        else
        {
            ErrCode nError = ERRCODE_NONE;
            pRet = CreateSdrOLEFromStorage(
                *pSdrModel,
                sStorageName,
                xSrcStg,
                xDstStg,
                rGrf,
                rBoundRect,
                rVisArea,
                pStData,
                nError,
                nSvxMSDffOLEConvFlags,
                css::embed::Aspects::MSOLE_CONTENT,
                m_rReader.GetBaseURL());
        }
    }
    return pRet;
}

void SwMSDffManager::DisableFallbackStream()
{
    OSL_ENSURE(!m_pFallbackStream,
        "if you're recursive, you're broken");
    m_pFallbackStream = pStData2;
    m_aOldEscherBlipCache = aEscherBlipCache;
    aEscherBlipCache.clear();
    pStData2 = nullptr;
}

void SwMSDffManager::EnableFallbackStream()
{
    pStData2 = m_pFallbackStream;
    aEscherBlipCache = m_aOldEscherBlipCache;
    m_aOldEscherBlipCache.clear();
    m_pFallbackStream = nullptr;
}

sal_uInt16 SwWW8ImplReader::GetToggleAttrFlags() const
{
    return m_xCtrlStck ? m_xCtrlStck->GetToggleAttrFlags() : 0;
}

sal_uInt16 SwWW8ImplReader::GetToggleBiDiAttrFlags() const
{
    return m_xCtrlStck ? m_xCtrlStck->GetToggleBiDiAttrFlags() : 0;
}

void SwWW8ImplReader::SetToggleAttrFlags(sal_uInt16 nFlags)
{
    if (m_xCtrlStck)
        m_xCtrlStck->SetToggleAttrFlags(nFlags);
}

void SwWW8ImplReader::SetToggleBiDiAttrFlags(sal_uInt16 nFlags)
{
    if (m_xCtrlStck)
        m_xCtrlStck->SetToggleBiDiAttrFlags(nFlags);
}

rtl::Reference<SdrObject> SwMSDffManager::ProcessObj(SvStream& rSt,
                                       DffObjData& rObjData,
                                       SvxMSDffClientData& rData,
                                       tools::Rectangle& rTextRect,
                                       SdrObject* pObj1
                                       )
{
    rtl::Reference<SdrObject> pObj = pObj1;
    if( !rTextRect.IsEmpty() )
    {
        SvxMSDffImportData& rImportData = static_cast<SvxMSDffImportData&>(rData);
        std::unique_ptr<SvxMSDffImportRec> pImpRec(new SvxMSDffImportRec);

        // fill Import Record with data
        pImpRec->nShapeId   = rObjData.nShapeId;
        pImpRec->eShapeType = rObjData.eShapeType;

        rObjData.bClientAnchor = maShapeRecords.SeekToContent( rSt,
                                            DFF_msofbtClientAnchor,
                                            SEEK_FROM_CURRENT_AND_RESTART );
        if( rObjData.bClientAnchor )
            ProcessClientAnchor( rSt,
                    maShapeRecords.Current()->nRecLen,
                    pImpRec->pClientAnchorBuffer, pImpRec->nClientAnchorLen );

        rObjData.bClientData = maShapeRecords.SeekToContent( rSt,
                                            DFF_msofbtClientData,
                                            SEEK_FROM_CURRENT_AND_RESTART );
        if( rObjData.bClientData )
            ProcessClientData( rSt,
                    maShapeRecords.Current()->nRecLen,
                    pImpRec->pClientDataBuffer, pImpRec->nClientDataLen );

        pImpRec->nGroupShapeBooleanProperties = 0;

        if(    maShapeRecords.SeekToContent( rSt,
                                             DFF_msofbtUDefProp,
                                             SEEK_FROM_CURRENT_AND_RESTART )
            && maShapeRecords.Current()->nRecLen )
        {
            sal_uInt32 nBytesLeft = maShapeRecords.Current()->nRecLen;
            auto nAvailableBytes = rSt.remainingSize();
            if (nBytesLeft > nAvailableBytes)
            {
                SAL_WARN("sw.ww8", "Document claimed to have shape record of " << nBytesLeft << " bytes, but only " << nAvailableBytes << " available");
                nBytesLeft = nAvailableBytes;
            }
            while( 5 < nBytesLeft )
            {
                sal_uInt16 nPID(0);
                rSt.ReadUInt16(nPID);
                sal_uInt32 nUDData(0);
                rSt.ReadUInt32(nUDData);
                if (!rSt.good())
                    break;
                switch (nPID)
                {
                    case 0x038F: pImpRec->nXAlign = nUDData; break;
                    case 0x0390:
                        pImpRec->nXRelTo = nUDData;
                        break;
                    case 0x0391: pImpRec->nYAlign = nUDData; break;
                    case 0x0392:
                        pImpRec->nYRelTo = nUDData;
                        break;
                    case 0x03BF: pImpRec->nGroupShapeBooleanProperties = nUDData; break;
                    case 0x0393:
                    // This seems to correspond to o:hrpct from .docx (even including
                    // the difference that it's in 0.1% even though the .docx spec
                    // says it's in 1%).
                        pImpRec->relativeHorizontalWidth = nUDData;
                        break;
                    case 0x0394:
                    // And this is really just a guess, but a mere presence of this
                    // flag makes a horizontal rule be as wide as the page (unless
                    // overridden by something), so it probably matches o:hr from .docx.
                        pImpRec->isHorizontalRule = true;
                        break;
                }
                nBytesLeft  -= 6;
            }
        }

        // Text Frame also Title or Outline
        sal_uInt32 nTextId = GetPropertyValue( DFF_Prop_lTxid, 0 );
        if( nTextId )
        {
            SfxItemSet aSet( pSdrModel->GetItemPool() );

            // Originally anything that as a mso_sptTextBox was created as a
            // textbox, this was changed to be created as a simple
            // rect to keep impress happy. For the rest of us we'd like to turn
            // it back into a textbox again.
            bool bIsSimpleDrawingTextBox = (pImpRec->eShapeType == mso_sptTextBox);
            if (!bIsSimpleDrawingTextBox)
            {
                // Either
                // a) it's a simple text object or
                // b) it's a rectangle with text and square wrapping.
                bIsSimpleDrawingTextBox =
                (
                    (pImpRec->eShapeType == mso_sptTextSimple) ||
                    (
                        (pImpRec->eShapeType == mso_sptRectangle)
                        && ShapeHasText(pImpRec->nShapeId, rObjData.rSpHd.GetRecBegFilePos() )
                    )
                );
            }

            // Distance of Textbox to its surrounding Autoshape
            sal_Int32 nTextLeft = GetPropertyValue( DFF_Prop_dxTextLeft, 91440);
            sal_Int32 nTextRight = GetPropertyValue( DFF_Prop_dxTextRight, 91440 );
            sal_Int32 nTextTop = GetPropertyValue( DFF_Prop_dyTextTop, 45720 );
            sal_Int32 nTextBottom = GetPropertyValue( DFF_Prop_dyTextBottom, 45720 );

            ScaleEmu( nTextLeft );
            ScaleEmu( nTextRight );
            ScaleEmu( nTextTop );
            ScaleEmu( nTextBottom );

            Degree100 nTextRotationAngle;
            bool bVerticalText = false;
            if ( IsProperty( DFF_Prop_txflTextFlow ) )
            {
                MSO_TextFlow eTextFlow = static_cast<MSO_TextFlow>(GetPropertyValue(
                    DFF_Prop_txflTextFlow, 0) & 0xFFFF);
                switch( eTextFlow )
                {
                    case mso_txflBtoT:
                        nTextRotationAngle = 9000_deg100;
                    break;
                    case mso_txflVertN:
                    case mso_txflTtoBN:
                        nTextRotationAngle = 27000_deg100;
                        break;
                    case mso_txflTtoBA:
                        bVerticalText = true;
                    break;
                    case mso_txflHorzA:
                        bVerticalText = true;
                        nTextRotationAngle = 9000_deg100;
                    break;
                    case mso_txflHorzN:
                    default :
                        break;
                }
            }

            if (nTextRotationAngle)
            {
                if (nTextRotationAngle == 9000_deg100)
                {
                    tools::Long nWidth = rTextRect.GetWidth();
                    rTextRect.SetRight( rTextRect.Left() + rTextRect.GetHeight() );
                    rTextRect.SetBottom( rTextRect.Top() + nWidth );

                    sal_Int32 nOldTextLeft = nTextLeft;
                    sal_Int32 nOldTextRight = nTextRight;
                    sal_Int32 nOldTextTop = nTextTop;
                    sal_Int32 nOldTextBottom = nTextBottom;

                    nTextLeft = nOldTextBottom;
                    nTextRight = nOldTextTop;
                    nTextTop = nOldTextLeft;
                    nTextBottom = nOldTextRight;
                }
                else if (nTextRotationAngle == 27000_deg100)
                {
                    tools::Long nWidth = rTextRect.GetWidth();
                    rTextRect.SetRight( rTextRect.Left() + rTextRect.GetHeight() );
                    rTextRect.SetBottom( rTextRect.Top() + nWidth );

                    sal_Int32 nOldTextLeft = nTextLeft;
                    sal_Int32 nOldTextRight = nTextRight;
                    sal_Int32 nOldTextTop = nTextTop;
                    sal_Int32 nOldTextBottom = nTextBottom;

                    nTextLeft = nOldTextTop;
                    nTextRight = nOldTextBottom;
                    nTextTop = nOldTextRight;
                    nTextBottom = nOldTextLeft;
                }
            }

            if (bIsSimpleDrawingTextBox)
            {
                pObj = new SdrRectObj(
                    *pSdrModel, rTextRect,SdrObjKind::Text );
            }

            // The vertical paragraph justification are contained within the
            // BoundRect so calculate it here
            tools::Rectangle aNewRect(rTextRect);
            aNewRect.AdjustBottom( -(nTextTop + nTextBottom) );
            aNewRect.AdjustRight( -(nTextLeft + nTextRight) );

            // Only if it's a simple Textbox, Writer can replace the Object
            // with a Frame, else
            if( bIsSimpleDrawingTextBox )
            {
                std::shared_ptr<SvxMSDffShapeInfo> const xTmpRec =
                        std::make_shared<SvxMSDffShapeInfo>(0, pImpRec->nShapeId);

                SvxMSDffShapeInfos_ById::const_iterator const it =
                    GetShapeInfos()->find(xTmpRec);
                if (it != GetShapeInfos()->end())
                {
                    SvxMSDffShapeInfo& rInfo = **it;
                    pImpRec->bReplaceByFly   = rInfo.bReplaceByFly;
                }

                ApplyAttributes(rSt, aSet, rObjData);
            }

            if (GetPropertyValue(DFF_Prop_FitTextToShape, 0) & 2)
            {
                aSet.Put( makeSdrTextAutoGrowHeightItem( true ) );
                aSet.Put( makeSdrTextMinFrameHeightItem(
                    aNewRect.Bottom() - aNewRect.Top() ) );
                aSet.Put( makeSdrTextMinFrameWidthItem(
                    aNewRect.Right() - aNewRect.Left() ) );
            }
            else
            {
                aSet.Put( makeSdrTextAutoGrowHeightItem( false ) );
                aSet.Put( makeSdrTextAutoGrowWidthItem( false ) );
            }

            switch ( static_cast<MSO_WrapMode>(GetPropertyValue( DFF_Prop_WrapText, mso_wrapSquare )) )
            {
                case mso_wrapNone :
                    aSet.Put( makeSdrTextAutoGrowWidthItem( true ) );
                    pImpRec->bAutoWidth = true;
                break;
                case mso_wrapByPoints :
                    aSet.Put( makeSdrTextContourFrameItem( true ) );
                break;
                default:
                    ;
            }

            // Set distances on Textbox's margins
            aSet.Put( makeSdrTextLeftDistItem( nTextLeft ) );
            aSet.Put( makeSdrTextRightDistItem( nTextRight ) );
            aSet.Put( makeSdrTextUpperDistItem( nTextTop ) );
            aSet.Put( makeSdrTextLowerDistItem( nTextBottom ) );
            pImpRec->nDxTextLeft    = nTextLeft;
            pImpRec->nDyTextTop     = nTextTop;
            pImpRec->nDxTextRight   = nTextRight;
            pImpRec->nDyTextBottom  = nTextBottom;

            // Taking the correct default (which is mso_anchorTop)
            sal_uInt32 eTextAnchor =
                GetPropertyValue( DFF_Prop_anchorText, mso_anchorTop );

            SdrTextVertAdjust eTVA = bVerticalText
                                     ? SDRTEXTVERTADJUST_BLOCK
                                     : SDRTEXTVERTADJUST_CENTER;
            SdrTextHorzAdjust eTHA = bVerticalText
                                     ? SDRTEXTHORZADJUST_CENTER
                                     : SDRTEXTHORZADJUST_BLOCK;

            switch( eTextAnchor )
            {
                case mso_anchorTop:
                case mso_anchorTopCentered:
                {
                    if ( bVerticalText )
                        eTHA = SDRTEXTHORZADJUST_RIGHT;
                    else
                        eTVA = SDRTEXTVERTADJUST_TOP;
                }
                break;
                case mso_anchorMiddle:
                break;
                case mso_anchorMiddleCentered:
                break;
                case mso_anchorBottom:
                case mso_anchorBottomCentered:
                {
                    if ( bVerticalText )
                        eTHA = SDRTEXTHORZADJUST_LEFT;
                    else
                        eTVA = SDRTEXTVERTADJUST_BOTTOM;
                }
                break;
                default:
                    ;
            }

            aSet.Put( SdrTextVertAdjustItem( eTVA ) );
            aSet.Put( SdrTextHorzAdjustItem( eTHA ) );

            if (pObj != nullptr)
            {
                pObj->SetMergedItemSet(aSet);

                if (bVerticalText)
                {
                    SdrTextObj *pTextObj = DynCastSdrTextObj(pObj.get());
                    if (pTextObj)
                        pTextObj->SetVerticalWriting(true);
                }

                if ( bIsSimpleDrawingTextBox )
                {
                    if ( nTextRotationAngle )
                    {
                        tools::Long nMinWH = rTextRect.GetWidth() < rTextRect.GetHeight() ?
                            rTextRect.GetWidth() : rTextRect.GetHeight();
                        nMinWH /= 2;
                        Point aPivot(rTextRect.TopLeft());
                        aPivot.AdjustX(nMinWH );
                        aPivot.AdjustY(nMinWH );
                        pObj->NbcRotate(aPivot, nTextRotationAngle);
                    }
                }

                if ( ( ( rObjData.nSpFlags & ShapeFlag::FlipV ) || mnFix16Angle || nTextRotationAngle ) && dynamic_cast< SdrObjCustomShape* >( pObj.get() ) )
                {
                    SdrObjCustomShape* pCustomShape = dynamic_cast< SdrObjCustomShape* >( pObj.get() );
                    if (pCustomShape)
                    {
                        double fExtraTextRotation = 0.0;
                        if ( mnFix16Angle && !( GetPropertyValue( DFF_Prop_FitTextToShape, 0 ) & 4 ) )
                        {   // text is already rotated, we have to take back the object rotation if DFF_Prop_RotateText is false
                            fExtraTextRotation = -mnFix16Angle.get();
                        }
                        if ( rObjData.nSpFlags & ShapeFlag::FlipV )    // sj: in ppt the text is flipped, whereas in word the text
                        {                                       // remains unchanged, so we have to take back the flipping here
                            fExtraTextRotation += 18000.0;      // because our core will flip text if the shape is flipped.
                        }
                        fExtraTextRotation += nTextRotationAngle.get();
                        if ( !::basegfx::fTools::equalZero( fExtraTextRotation ) )
                        {
                            fExtraTextRotation /= 100.0;
                            SdrCustomShapeGeometryItem aGeometryItem( pCustomShape->GetMergedItem( SDRATTR_CUSTOMSHAPE_GEOMETRY ) );
                            css::beans::PropertyValue aPropVal;
                            aPropVal.Name = "TextRotateAngle";
                            aPropVal.Value <<= fExtraTextRotation;
                            aGeometryItem.SetPropertyValue( aPropVal );
                            pCustomShape->SetMergedItem( aGeometryItem );
                        }
                    }
                }
                else if ( mnFix16Angle )
                {
                    // rotate text with shape ?
                    pObj->NbcRotate( rObjData.aBoundRect.Center(), mnFix16Angle );
                }
            }
        }
        else if( !pObj )
        {
            // simple rectangular objects are ignored by ImportObj()  :-(
            // this is OK for Draw but not for Calc and Writer
            // cause here these objects have a default border
            pObj = new SdrRectObj(
                *pSdrModel,
                rTextRect);

            SfxItemSet aSet( pSdrModel->GetItemPool() );
            ApplyAttributes( rSt, aSet, rObjData );

            SfxItemState eState = aSet.GetItemState( XATTR_FILLCOLOR, false );
            if( SfxItemState::DEFAULT == eState )
                aSet.Put( XFillColorItem( OUString(), mnDefaultColor ) );
            pObj->SetMergedItemSet(aSet);
        }

        // Means that fBehindDocument is set
        if (GetPropertyValue(DFF_Prop_fPrint, 0) & 0x20)
            pImpRec->bDrawHell = true;
        else
            pImpRec->bDrawHell = false;
        if (GetPropertyValue(DFF_Prop_fPrint, 0) & 0x02)
            pImpRec->bHidden = true;
        pImpRec->nNextShapeId   = GetPropertyValue( DFF_Prop_hspNext, 0 );

        if ( nTextId )
        {
            pImpRec->aTextId.nTxBxS = o3tl::narrowing<sal_uInt16>( nTextId >> 16 );
            pImpRec->aTextId.nSequence = o3tl::narrowing<sal_uInt16>(nTextId);
        }

        pImpRec->nDxWrapDistLeft = o3tl::convert(GetPropertyValue(DFF_Prop_dxWrapDistLeft, 114935),
                                                 o3tl::Length::emu, o3tl::Length::twip);
        pImpRec->nDyWrapDistTop = o3tl::convert(GetPropertyValue(DFF_Prop_dyWrapDistTop, 0),
                                                o3tl::Length::emu, o3tl::Length::twip);
        pImpRec->nDxWrapDistRight
            = o3tl::convert(GetPropertyValue(DFF_Prop_dxWrapDistRight, 114935), o3tl::Length::emu,
                            o3tl::Length::twip);
        pImpRec->nDyWrapDistBottom = o3tl::convert(GetPropertyValue(DFF_Prop_dyWrapDistBottom, 0),
                                                   o3tl::Length::emu, o3tl::Length::twip);
        // 16.16 fraction times total image width or height, as appropriate.

        if (SeekToContent(DFF_Prop_pWrapPolygonVertices, rSt))
        {
            pImpRec->pWrapPolygon.reset();

            sal_uInt16 nNumElemVert(0), nNumElemMemVert(0), nElemSizeVert(0);
            rSt.ReadUInt16( nNumElemVert ).ReadUInt16( nNumElemMemVert ).ReadUInt16( nElemSizeVert );
            bool bOk = false;
            if (nNumElemVert && (nElemSizeVert == 8 || nElemSizeVert == 4))
            {
                //check if there is enough data in the file to make the
                //record sane
                // coverity[tainted_data : FALSE] - nElemSizeVert is either 8 or 4 so it has been sanitized
                bOk = rSt.remainingSize() / nElemSizeVert >= nNumElemVert;
            }
            if (bOk)
            {
                pImpRec->pWrapPolygon = tools::Polygon(nNumElemVert);
                for (sal_uInt16 i = 0; i < nNumElemVert; ++i)
                {
                    sal_Int32 nX(0), nY(0);
                    if (nElemSizeVert == 8)
                        rSt.ReadInt32( nX ).ReadInt32( nY );
                    else
                    {
                        sal_Int16 nSmallX(0), nSmallY(0);
                        rSt.ReadInt16( nSmallX ).ReadInt16( nSmallY );
                        nX = nSmallX;
                        nY = nSmallY;
                    }
                    (*(pImpRec->pWrapPolygon))[i].setX( nX );
                    (*(pImpRec->pWrapPolygon))[i].setY( nY );
                }
            }
        }

        pImpRec->nCropFromTop = GetPropertyValue(
                                    DFF_Prop_cropFromTop, 0 );
        pImpRec->nCropFromBottom = GetPropertyValue(
                                    DFF_Prop_cropFromBottom, 0 );
        pImpRec->nCropFromLeft = GetPropertyValue(
                                    DFF_Prop_cropFromLeft, 0 );
        pImpRec->nCropFromRight = GetPropertyValue(
                                    DFF_Prop_cropFromRight, 0 );

        sal_uInt32 nLineFlags = GetPropertyValue( DFF_Prop_fNoLineDrawDash, 0 );

        if ( !IsHardAttribute( DFF_Prop_fLine ) &&
             pImpRec->eShapeType == mso_sptPictureFrame )
        {
            nLineFlags &= ~0x08;
        }

        pImpRec->eLineStyle = (nLineFlags & 8)
                              ? static_cast<MSO_LineStyle>(GetPropertyValue(
                                                    DFF_Prop_lineStyle,
                                                    mso_lineSimple ))
                              : MSO_LineStyle(USHRT_MAX);
        pImpRec->eLineDashing = static_cast<MSO_LineDashing>(GetPropertyValue(
                                        DFF_Prop_lineDashing, mso_lineSolid ));

        pImpRec->nFlags = rObjData.nSpFlags;

        if( pImpRec->nShapeId )
        {
            auto nShapeId = pImpRec->nShapeId;
            auto nShapeOrder = (static_cast<sal_uLong>(pImpRec->aTextId.nTxBxS) << 16)
                                    + pImpRec->aTextId.nSequence;
            // Complement Import Record List
            pImpRec->pObj = pObj;
            rImportData.insert(std::move(pImpRec));

            // Complement entry in Z Order List with a pointer to this Object
            // Only store objects which are not deep inside the tree
            if( ( rObjData.nCalledByGroup == 0 )
                ||
                ( (rObjData.nSpFlags & ShapeFlag::Group)
                 && (rObjData.nCalledByGroup < 2) )
              )
            {
                StoreShapeOrder(nShapeId, nShapeOrder, pObj.get());
            }
        }
        else
            pImpRec.reset();
    }

    sal_uInt32 nBufferSize = GetPropertyValue( DFF_Prop_pihlShape, 0 );
    if( (0 < nBufferSize) && (nBufferSize <= 0xFFFF) && SeekToContent( DFF_Prop_pihlShape, rSt ) )
    {
        SvMemoryStream aMemStream;
        struct HyperLinksTable hlStr;
        aMemStream.WriteUInt16( 0 ).WriteUInt16( nBufferSize );

        // copy from DFF stream to memory stream
        std::vector< sal_uInt8 > aBuffer( nBufferSize );
        if (rSt.ReadBytes(aBuffer.data(), nBufferSize) == nBufferSize)
        {
            aMemStream.WriteBytes(aBuffer.data(), nBufferSize);
            sal_uInt64 nStreamSize = aMemStream.TellEnd();
            aMemStream.Seek( STREAM_SEEK_TO_BEGIN );
            bool bRet = 4 <= nStreamSize;
            sal_uInt16 nRawRecId,nRawRecSize;
            if( bRet )
                aMemStream.ReadUInt16( nRawRecId ).ReadUInt16( nRawRecSize );
            SwDocShell* pDocShell = m_rReader.m_pDocShell;
            if (pDocShell)
            {
                m_rReader.ReadEmbeddedData(aMemStream, pDocShell, hlStr);
            }
        }

        if (pObj && !hlStr.hLinkAddr.isEmpty())
        {
            SwMacroInfo* pInfo = GetMacroInfo( pObj.get() );
            if( pInfo )
            {
                pInfo->SetShapeId( rObjData.nShapeId );
                pInfo->SetHlink( hlStr.hLinkAddr );
                if (!hlStr.tarFrame.isEmpty())
                    pInfo->SetTarFrame( hlStr.tarFrame );
                OUString aNameStr = GetPropertyString( DFF_Prop_wzName, rSt );
                if (!aNameStr.isEmpty())
                    pInfo->SetName( aNameStr );
            }
        }
    }

    return pObj;
}

/**
 * Special FastSave - Attributes
 */
void SwWW8ImplReader::Read_StyleCode( sal_uInt16, const sal_uInt8* pData, short nLen )
{
    if (nLen < 0)
    {
        m_bCpxStyle = false;
        return;
    }
    sal_uInt16 nColl = 0;
    if (m_xWwFib->GetFIBVersion() <= ww::eWW2)
        nColl = *pData;
    else
        nColl = SVBT16ToUInt16(pData);
    if (nColl < m_vColl.size())
    {
        SetTextFormatCollAndListLevel( *m_pPaM, m_vColl[nColl] );
        m_bCpxStyle = true;
    }
}

/**
 * Read_Majority is for Majority (103) and Majority50 (108)
 */
void SwWW8ImplReader::Read_Majority( sal_uInt16, const sal_uInt8* , short )
{
}

/**
 * Stack
 */
void SwWW8FltControlStack::NewAttr(const SwPosition& rPos,
    const SfxPoolItem& rAttr)
{
    OSL_ENSURE(RES_TXTATR_FIELD != rAttr.Which(), "probably don't want to put"
        "fields into the control stack");
    OSL_ENSURE(RES_TXTATR_INPUTFIELD != rAttr.Which(), "probably don't want to put"
        "input fields into the control stack");
    OSL_ENSURE(RES_TXTATR_ANNOTATION != rAttr.Which(), "probably don't want to put"
        "annotations into the control stack");
    OSL_ENSURE(RES_FLTR_REDLINE != rAttr.Which(), "probably don't want to put"
        "redlines into the control stack");
    SwFltControlStack::NewAttr(rPos, rAttr);
}

SwFltStackEntry* SwWW8FltControlStack::SetAttr(const SwPosition& rPos, sal_uInt16 nAttrId,
    bool bTstEnd, tools::Long nHand, bool )
{
    SwFltStackEntry *pRet = nullptr;
    // Doing a textbox, and using the control stack only as a temporary
    // collection point for properties which will are not to be set into
    // the real document
    if (m_rReader.m_xPlcxMan && m_rReader.m_xPlcxMan->GetDoingDrawTextBox())
    {
        size_t nCnt = size();
        size_t i = 0;
        while (i < nCnt)
        {
            SwFltStackEntry& rEntry = (*this)[i];
            if (nAttrId == rEntry.m_pAttr->Which())
            {
                DeleteAndDestroy(i);
                --nCnt;
                break;
            }
            ++i;
        }
    }
    else // Normal case, set the attribute into the document
        pRet = SwFltControlStack::SetAttr(rPos, nAttrId, bTstEnd, nHand);
    return pRet;
}

tools::Long GetListFirstLineIndent(const SwNumFormat &rFormat)
{
    OSL_ENSURE( rFormat.GetPositionAndSpaceMode() == SvxNumberFormat::LABEL_WIDTH_AND_POSITION,
            "<GetListFirstLineIndent> - misusage: position-and-space-mode does not equal LABEL_WIDTH_AND_POSITION" );

    SvxAdjust eAdj = rFormat.GetNumAdjust();
    tools::Long nReverseListIndented;
    if (eAdj == SvxAdjust::Right)
        nReverseListIndented = -rFormat.GetCharTextDistance();
    else if (eAdj == SvxAdjust::Center)
        nReverseListIndented = rFormat.GetFirstLineOffset()/2;
    else
        nReverseListIndented = rFormat.GetFirstLineOffset();
    return nReverseListIndented;
}

static tools::Long lcl_GetTrueMargin(SvxFirstLineIndentItem const& rFirstLine,
        SvxTextLeftMarginItem const& rLeftMargin, const SwNumFormat &rFormat,
    tools::Long &rFirstLinePos)
{
    OSL_ENSURE( rFormat.GetPositionAndSpaceMode() == SvxNumberFormat::LABEL_WIDTH_AND_POSITION,
            "<lcl_GetTrueMargin> - misusage: position-and-space-mode does not equal LABEL_WIDTH_AND_POSITION" );

    const tools::Long nBodyIndent = rLeftMargin.ResolveTextLeft({});
    const tools::Long nFirstLineDiff = rFirstLine.ResolveTextFirstLineOffset({});
    rFirstLinePos = nBodyIndent + nFirstLineDiff;

    const auto nPseudoListBodyIndent = rFormat.GetAbsLSpace();
    const tools::Long nReverseListIndented = GetListFirstLineIndent(rFormat);
    tools::Long nExtraListIndent = nPseudoListBodyIndent + nReverseListIndented;

    return std::max<tools::Long>(nExtraListIndent, 0);
}

// #i103711#
// #i105414#
void SyncIndentWithList( SvxFirstLineIndentItem & rFirstLine,
                         SvxTextLeftMarginItem & rLeftMargin,
                         const SwNumFormat &rFormat,
                         const bool bFirstLineOfstSet,
                         const bool bLeftIndentSet )
{
    if ( rFormat.GetPositionAndSpaceMode() == SvxNumberFormat::LABEL_WIDTH_AND_POSITION )
    {
        tools::Long nWantedFirstLinePos;
        tools::Long nExtraListIndent = lcl_GetTrueMargin(rFirstLine, rLeftMargin, rFormat, nWantedFirstLinePos);
        rLeftMargin.SetTextLeft(SvxIndentValue::twips(nWantedFirstLinePos - nExtraListIndent));
        rFirstLine.SetTextFirstLineOffset(SvxIndentValue::zero());
    }
    else if ( rFormat.GetPositionAndSpaceMode() == SvxNumberFormat::LABEL_ALIGNMENT )
    {
        if ( !bFirstLineOfstSet && bLeftIndentSet &&
             rFormat.GetFirstLineIndent() != 0 )
        {
            rFirstLine.SetTextFirstLineOffset(
                SvxIndentValue{ static_cast<double>(rFormat.GetFirstLineIndent()),
                                rFormat.GetFirstLineIndentUnit() });
        }
        else if ( bFirstLineOfstSet && !bLeftIndentSet &&
                  rFormat.GetIndentAt() != 0 )
        {
            rLeftMargin.SetTextLeft(SvxIndentValue::twips(rFormat.GetIndentAt()));
        }
        else if (!bFirstLineOfstSet && !bLeftIndentSet )
        {
            if ( rFormat.GetFirstLineIndent() != 0 )
            {
                rFirstLine.SetTextFirstLineOffset(
                    SvxIndentValue{ static_cast<double>(rFormat.GetFirstLineIndent()),
                                    rFormat.GetFirstLineIndentUnit() });
            }
            if ( rFormat.GetIndentAt() != 0 )
            {
                rLeftMargin.SetTextLeft(SvxIndentValue::twips(rFormat.GetIndentAt()));
            }
        }
    }
}

const SwNumFormat* SwWW8FltControlStack::GetNumFormatFromStack(const SwPosition &rPos,
    const SwTextNode &rTextNode)
{
    const SwNumFormat *pRet = nullptr;
    const SfxPoolItem *pItem = GetStackAttr(rPos, RES_FLTR_NUMRULE);
    if (pItem && rTextNode.GetNumRule())
    {
        if (rTextNode.IsCountedInList())
        {
            OUString sName(static_cast<const SfxStringItem*>(pItem)->GetValue());
            const SwNumRule *pRule = m_rDoc.FindNumRulePtr(UIName(sName));
            if (pRule)
                pRet = GetNumFormatFromSwNumRuleLevel(*pRule, rTextNode.GetActualListLevel());
        }
    }
    return pRet;
}

void SwWW8ReferencedFltEndStack::SetAttrInDoc( const SwPosition& rTmpPos,
                                               SwFltStackEntry& rEntry )
{
    switch( rEntry.m_pAttr->Which() )
    {
    case RES_FLTR_BOOKMARK:
        {
            // suppress insertion of bookmark, which is recognized as an internal bookmark used for table-of-content
            // and which is not referenced.
            bool bInsertBookmarkIntoDoc = true;

            SwFltBookmark* pFltBookmark = dynamic_cast<SwFltBookmark*>(rEntry.m_pAttr.get());
            if ( pFltBookmark != nullptr && pFltBookmark->IsTOCBookmark() )
            {
                const OUString& rName = pFltBookmark->GetName();
                std::set< OUString, SwWW8::ltstr >::const_iterator aResult = m_aReferencedTOCBookmarks.find(rName);
                if ( aResult == m_aReferencedTOCBookmarks.end() )
                {
                    bInsertBookmarkIntoDoc = false;
                }
            }
            if ( bInsertBookmarkIntoDoc )
            {
                SwFltEndStack::SetAttrInDoc( rTmpPos, rEntry );
            }
            break;
        }
    default:
        SwFltEndStack::SetAttrInDoc( rTmpPos, rEntry );
        break;
    }

}

void SwWW8FltControlStack::SetAttrInDoc(const SwPosition& rTmpPos,
    SwFltStackEntry& rEntry)
{
    switch (rEntry.m_pAttr->Which())
    {
        case RES_LR_SPACE:
            assert(false);
            break;
        case RES_MARGIN_FIRSTLINE:
        case RES_MARGIN_TEXTLEFT:
            {
                /*
                 Loop over the affected nodes and
                 a) convert the word style absolute indent to indent relative
                    to any numbering indent active on the nodes
                 b) adjust the writer style tabstops relative to the old
                    paragraph indent to be relative to the new paragraph indent
                */
                SwPaM aRegion(rTmpPos);
                if (rEntry.MakeRegion(aRegion, SwFltStackEntry::RegionMode::NoCheck))
                {
                    SvxFirstLineIndentItem firstLineNew(RES_MARGIN_FIRSTLINE);
                    SvxTextLeftMarginItem leftMarginNew(RES_MARGIN_TEXTLEFT);
                    if (rEntry.m_pAttr->Which() == RES_MARGIN_FIRSTLINE)
                    {
                        SvxFirstLineIndentItem const firstLineEntry(*static_cast<SvxFirstLineIndentItem*>(rEntry.m_pAttr.get()));
                        firstLineNew.SetTextFirstLineOffset(
                            firstLineEntry.GetTextFirstLineOffset(),
                            firstLineEntry.GetPropTextFirstLineOffset());
                        firstLineNew.SetAutoFirst(firstLineEntry.IsAutoFirst());
                    }
                    else
                    {
                        SvxTextLeftMarginItem const leftMarginEntry(*static_cast<SvxTextLeftMarginItem*>(rEntry.m_pAttr.get()));
                        leftMarginNew.SetTextLeft(leftMarginEntry.GetTextLeft(), leftMarginEntry.GetPropLeft());
                    }
                    SwNodeOffset nStart = aRegion.Start()->GetNodeIndex();
                    SwNodeOffset nEnd   = aRegion.End()->GetNodeIndex();
                    for(; nStart <= nEnd; ++nStart)
                    {
                        SwNode* pNode = m_rDoc.GetNodes()[ nStart ];
                        if (!pNode || !pNode->IsTextNode())
                            continue;

                        SwContentNode* pNd = static_cast<SwContentNode*>(pNode);
                        SvxFirstLineIndentItem firstLineOld(pNd->GetAttr(RES_MARGIN_FIRSTLINE));
                        SvxTextLeftMarginItem leftMarginOld(pNd->GetAttr(RES_MARGIN_TEXTLEFT));
                        if (rEntry.m_pAttr->Which() == RES_MARGIN_FIRSTLINE)
                        {
                            leftMarginNew.SetTextLeft(leftMarginOld.GetTextLeft(), leftMarginOld.GetPropLeft());
                        }
                        else
                        {
                            firstLineNew.SetTextFirstLineOffset(
                                firstLineOld.GetTextFirstLineOffset(),
                                firstLineOld.GetPropTextFirstLineOffset());
                            firstLineNew.SetAutoFirst(firstLineOld.IsAutoFirst());
                        }

                        SwTextNode *pTextNode = static_cast<SwTextNode*>(pNode);

                        const SwNumFormat* pNum
                            = GetNumFormatFromStack(*aRegion.GetPoint(), *pTextNode);
                        if (!pNum)
                        {
                            pNum = GetNumFormatFromTextNode(*pTextNode);
                        }

                        if ( pNum )
                        {
                            // #i103711#
                            const bool bFirstLineIndentSet =
                                ( m_rReader.m_aTextNodesHavingFirstLineOfstSet.end() !=
                                    m_rReader.m_aTextNodesHavingFirstLineOfstSet.find( pNode ) );
                            // #i105414#
                            const bool bLeftIndentSet =
                                (  m_rReader.m_aTextNodesHavingLeftIndentSet.end() !=
                                    m_rReader.m_aTextNodesHavingLeftIndentSet.find( pNode ) );
                            SyncIndentWithList(firstLineNew, leftMarginNew, *pNum,
                                                bFirstLineIndentSet,
                                                bLeftIndentSet );
                        }

                        if (firstLineNew != firstLineOld)
                        {
                            if (nStart == aRegion.Start()->GetNodeIndex())
                            {
                                pNd->SetAttr(firstLineNew);
                            }
                        }
                        if (leftMarginNew != leftMarginOld)
                        {
                            pNd->SetAttr(leftMarginNew);
                        }
                    }
                }
            }
            break;

        case RES_TXTATR_FIELD:
            OSL_ENSURE(false, "What is a field doing in the control stack,"
                "probably should have been in the endstack");
            break;

        case RES_TXTATR_ANNOTATION:
            OSL_ENSURE(false, "What is an annotation doing in the control stack,"
                "probably should have been in the endstack");
            break;

        case RES_TXTATR_INPUTFIELD:
            OSL_ENSURE(false, "What is an input field doing in the control stack,"
                "probably should have been in the endstack");
            break;

        case RES_TXTATR_INETFMT:
            {
                SwPaM aRegion(rTmpPos);
                if (rEntry.MakeRegion(aRegion, SwFltStackEntry::RegionMode::NoCheck))
                {
                    SwFrameFormat *pFrame;
                    // If we have just one single inline graphic then
                    // don't insert a field for the single frame, set
                    // the frames hyperlink field attribute directly.
                    pFrame = SwWW8ImplReader::ContainsSingleInlineGraphic(aRegion);
                    if (nullptr != pFrame)
                    {
                        const SwFormatINetFormat *pAttr = static_cast<const SwFormatINetFormat *>(
                            rEntry.m_pAttr.get());
                        SwFormatURL aURL;
                        aURL.SetURL(pAttr->GetValue(), false);
                        aURL.SetTargetFrameName(pAttr->GetTargetFrame());
                        pFrame->SetFormatAttr(aURL);
                    }
                    else
                    {
                        m_rDoc.getIDocumentContentOperations().InsertPoolItem(aRegion, *rEntry.m_pAttr);
                    }
                }
            }
            break;
        default:
            SwFltControlStack::SetAttrInDoc(rTmpPos, rEntry);
            break;
    }
}

const SfxPoolItem* SwWW8FltControlStack::GetFormatAttr(const SwPosition& rPos,
    sal_uInt16 nWhich)
{
    const SfxPoolItem *pItem = GetStackAttr(rPos, nWhich);
    if (!pItem)
    {
        SwContentNode const*const pNd = rPos.GetNode().GetContentNode();
        if (!pNd)
            pItem = &m_rDoc.GetAttrPool().GetUserOrPoolDefaultItem(nWhich);
        else
        {
            /*
            If we're hunting for the indent on a paragraph and need to use the
            parent style indent, then return the indent in msword format, and
            not writer format, because that's the style that the filter works
            in (naturally)
            */
            if (nWhich == RES_MARGIN_FIRSTLINE
                || nWhich == RES_MARGIN_TEXTLEFT
                || nWhich == RES_MARGIN_RIGHT)
            {
                SfxItemState eState = SfxItemState::DEFAULT;
                if (const SfxItemSet *pSet = pNd->GetpSwAttrSet())
                    eState = pSet->GetItemState(nWhich, false);
                if (eState != SfxItemState::SET && m_rReader.m_nCurrentColl < m_rReader.m_vColl.size())
                {
                    switch (nWhich)
                    {
                        case RES_MARGIN_FIRSTLINE:
                            pItem = m_rReader.m_vColl[m_rReader.m_nCurrentColl].m_pWordFirstLine.get();
                            break;
                        case RES_MARGIN_TEXTLEFT:
                            pItem = m_rReader.m_vColl[m_rReader.m_nCurrentColl].m_pWordLeftMargin.get();
                            break;
                        case RES_MARGIN_RIGHT:
                            pItem = m_rReader.m_vColl[m_rReader.m_nCurrentColl].m_pWordRightMargin.get();
                            break;
                    }
                }
            }

            /*
            If we're hunting for a character property, try and exact position
            within the text node for lookup
            */
            if (pNd->IsTextNode())
            {
                const sal_Int32 nPos = rPos.GetContentIndex();
                m_xScratchSet.reset(new SfxItemSet(m_rDoc.GetAttrPool(), nWhich, nWhich));
                if (pNd->GetTextNode()->GetParaAttr(*m_xScratchSet, nPos, nPos))
                    pItem = m_xScratchSet->GetItem(nWhich);
            }

            if (!pItem)
                pItem = &pNd->GetAttr(nWhich);
        }
    }
    return pItem;
}

const SfxPoolItem* SwWW8FltControlStack::GetStackAttr(const SwPosition& rPos,
    sal_uInt16 nWhich)
{
    SwFltPosition aFltPos(rPos);

    size_t nSize = size();
    while (nSize)
    {
        const SwFltStackEntry& rEntry = (*this)[ --nSize ];
        if (rEntry.m_pAttr->Which() == nWhich)
        {
            if ( (rEntry.m_bOpen) ||
                 (
                  (rEntry.m_aMkPos.m_nNode <= aFltPos.m_nNode) &&
                  (rEntry.m_aPtPos.m_nNode >= aFltPos.m_nNode) &&
                  (rEntry.m_aMkPos.m_nContent <= aFltPos.m_nContent) &&
                  (rEntry.m_aPtPos.m_nContent > aFltPos.m_nContent)
                 )
               )
                /*
                 * e.g. half-open range [0-3) so asking for properties at 3
                 * means props that end at 3 are not included
                 */
            {
                return rEntry.m_pAttr.get();
            }
        }
    }
    return nullptr;
}

bool SwWW8FltRefStack::IsFootnoteEdnBkmField(
    const SwFormatField& rFormatField,
    sal_uInt16& rBkmNo)
{
    const SwField* pField = rFormatField.GetField();
    if (!pField)
        return false;
    if(SwFieldIds::GetRef != pField->Which())
        return false;
    auto pGetRefField = static_cast<const SwGetRefField*>(pField);
    ReferencesSubtype nSubType = pGetRefField->GetSubType();
    if( ((ReferencesSubtype::Footnote == nSubType) || (ReferencesSubtype::Endnote  == nSubType))
        && !pGetRefField->GetSetRefName().isEmpty())
    {
        const IDocumentMarkAccess* const pMarkAccess = m_rDoc.getIDocumentMarkAccess();
        auto ppBkmk =
            pMarkAccess->findMark( pGetRefField->GetSetRefName() );
        if(ppBkmk != pMarkAccess->getAllMarksEnd())
        {
            // find Sequence No of corresponding Foot-/Endnote
            rBkmNo = ppBkmk - pMarkAccess->getAllMarksBegin();
            return true;
        }
    }
    return false;
}

void SwWW8FltRefStack::SetAttrInDoc(const SwPosition& rTmpPos,
    SwFltStackEntry& rEntry)
{
    switch (rEntry.m_pAttr->Which())
    {
        /*
        Look up these in our lists of bookmarks that were changed to
        variables, and replace the ref field with a var field, otherwise
        do normal (?) strange stuff
        */
        case RES_TXTATR_FIELD:
        case RES_TXTATR_ANNOTATION:
        case RES_TXTATR_INPUTFIELD:
        {
            SwPaM aPaM(rEntry.m_aMkPos.m_nNode.GetNode(), SwNodeOffset(1), rEntry.m_aMkPos.m_nContent);

            SwFormatField& rFormatField   = *static_cast<SwFormatField*>(rEntry.m_pAttr.get());
            SwField* pField = rFormatField.GetField();

            if (!RefToVar(pField, rEntry))
            {
                sal_uInt16 nBkmNo;
                if( IsFootnoteEdnBkmField(rFormatField, nBkmNo) )
                {
                    ::sw::mark::MarkBase const * const pMark = m_rDoc.getIDocumentMarkAccess()->getAllMarksBegin()[nBkmNo];

                    const SwPosition& rBkMrkPos = pMark->GetMarkPos();

                    SwTextNode* pText = rBkMrkPos.GetNode().GetTextNode();
                    if( pText && rBkMrkPos.GetContentIndex() )
                    {
                        SwTextAttr* const pFootnote = pText->GetTextAttrForCharAt(
                            rBkMrkPos.GetContentIndex()-1, RES_TXTATR_FTN );
                        if( pFootnote )
                        {
                            sal_uInt16 nRefNo = static_cast<SwTextFootnote*>(pFootnote)->GetSeqRefNo();

                            static_cast<SwGetRefField*>(pField)->SetSeqNo( nRefNo );

                            if( pFootnote->GetFootnote().IsEndNote() )
                                static_cast<SwGetRefField*>(pField)->SetSubType(ReferencesSubtype::Endnote);
                        }
                    }
                }
            }

            m_rDoc.getIDocumentContentOperations().InsertPoolItem(aPaM, *rEntry.m_pAttr);
            MoveAttrs(*aPaM.GetPoint());
        }
        break;
        case RES_FLTR_TOX:
            SwFltEndStack::SetAttrInDoc(rTmpPos, rEntry);
            break;
        default:
        case RES_FLTR_BOOKMARK:
            OSL_ENSURE(false, "EndStck used with non field, not what we want");
            SwFltEndStack::SetAttrInDoc(rTmpPos, rEntry);
            break;
    }
}

/*
 For styles we will do our tabstop arithmetic in word style and adjust them to
 writer style after all the styles have been finished and the dust settles as
 to what affects what.

 For explicit attributes we turn the adjusted writer tabstops back into 0 based
 word indexes and we'll turn them back into writer indexes when setting them
 into the document. If explicit left indent exist which affects them, then this
 is handled when the explicit left indent is set into the document
*/
void SwWW8ImplReader::Read_Tab(sal_uInt16 , const sal_uInt8* pData, short nLen)
{
    if (nLen < 0)
    {
        m_xCtrlStck->SetAttr(*m_pPaM->GetPoint(), RES_PARATR_TABSTOP);
        return;
    }

    sal_uInt8 nDel = (nLen > 0) ? pData[0] : 0;
    const sal_uInt8* pDel = pData + 1;                   // Del - Array

    sal_uInt8 nIns = (nLen > nDel*2+1) ? pData[nDel*2+1] : 0;
    const sal_uInt8* pIns = pData + 2*nDel + 2;          // Ins - Array

    short nRequiredLength = 2 + 2*nDel + 2*nIns + 1*nIns;
    if (nRequiredLength > nLen)
    {
        // would require more data than available to describe!
        // discard invalid record
        nIns = 0;
        nDel = 0;
    }

    WW8_TBD const * pTyp = reinterpret_cast<WW8_TBD const *>(pData + 2*nDel + 2*nIns + 2); // Type Array

    std::shared_ptr<SvxTabStopItem> aAttr(std::make_shared<SvxTabStopItem>(0, 0, SvxTabAdjust::Default, RES_PARATR_TABSTOP));

    const SwFormat * pSty = nullptr;
    sal_uInt16 nTabBase;
    if (m_pCurrentColl && m_nCurrentColl < m_vColl.size()) // StyleDef
    {
        nTabBase = m_vColl[m_nCurrentColl].m_nBase;
        if (nTabBase < m_vColl.size())  // Based On
            pSty = m_vColl[nTabBase].m_pFormat;
    }
    else
    { // Text
        nTabBase = m_nCurrentColl;
        if (m_nCurrentColl < m_vColl.size())
            pSty = m_vColl[m_nCurrentColl].m_pFormat;
        //TODO: figure out else here
    }

    bool bFound = false;
    std::unordered_set<size_t> aLoopWatch;
    while (pSty && !bFound)
    {
        const SvxTabStopItem* pTabs;
        bFound = pSty->GetAttrSet().GetItemState(RES_PARATR_TABSTOP, false,
            &pTabs) == SfxItemState::SET;
        if( bFound )
        {
            aAttr.reset(pTabs->Clone());
        }
        else
        {
            sal_uInt16 nOldTabBase = nTabBase;
            // If based on another
            if (nTabBase < m_vColl.size())
                nTabBase = m_vColl[nTabBase].m_nBase;

            if (
                    nTabBase < m_vColl.size() &&
                    nOldTabBase != nTabBase &&
                    nTabBase != ww::stiNil
               )
            {
                // #i61789: Stop searching when next style is the same as the
                // current one (prevent loop)
                aLoopWatch.insert(reinterpret_cast<size_t>(pSty));
                if (nTabBase < m_vColl.size())
                    pSty = m_vColl[nTabBase].m_pFormat;
                //TODO figure out the else branch

                if (aLoopWatch.find(reinterpret_cast<size_t>(pSty)) !=
                    aLoopWatch.end())
                    pSty = nullptr;
            }
            else
                pSty = nullptr; // Give up on the search
        }
    }

    SvxTabStop aTabStop;
    for (short i=0; i < nDel; ++i)
    {
        sal_uInt16 nPos = aAttr->GetPos(SVBT16ToUInt16(pDel + i*2));
        if( nPos != SVX_TAB_NOTFOUND )
            aAttr->Remove( nPos );
    }

    for (short i=0; i < nIns; ++i)
    {
        short nPos = SVBT16ToUInt16(pIns + i*2);
        aTabStop.GetTabPos() = nPos;
        switch( pTyp[i].aBits1 & 0x7 ) // pTyp[i].jc
        {
            case 0:
                aTabStop.GetAdjustment() = SvxTabAdjust::Left;
                break;
            case 1:
                aTabStop.GetAdjustment() = SvxTabAdjust::Center;
                break;
            case 2:
                aTabStop.GetAdjustment() = SvxTabAdjust::Right;
                break;
            case 3:
                aTabStop.GetAdjustment() = SvxTabAdjust::Decimal;
                break;
            case 4:
                continue; // Ignore Bar
        }

        switch( pTyp[i].aBits1 >> 3 & 0x7 )
        {
            case 0:
                aTabStop.GetFill() = ' ';
                break;
            case 1:
                aTabStop.GetFill() = '.';
                break;
            case 2:
                aTabStop.GetFill() = '-';
                break;
            case 3:
            case 4:
                aTabStop.GetFill() = '_';
                break;
        }

        sal_uInt16 nPos2 = aAttr->GetPos( nPos );
        if (nPos2 != SVX_TAB_NOTFOUND)
            aAttr->Remove(nPos2); // Or else Insert() refuses
        aAttr->Insert(aTabStop);
    }

    if (nIns || nDel)
        NewAttr(*aAttr);
    else
    {
        // Here we have a tab definition which inserts no extra tabs, or deletes
        // no existing tabs. An older version of writer is probably the creator
        // of the document  :-( . So if we are importing a style we can just
        // ignore it. But if we are importing into text we cannot as during
        // text SwWW8ImplReader::Read_Tab is called at the begin and end of
        // the range the attrib affects, and ignoring it would upset the
        // balance
        if (!m_pCurrentColl) // not importing into a style
        {
            SvxTabStopItem aOrig = pSty ?
                pSty->GetFormatAttr(RES_PARATR_TABSTOP) :
                m_rDoc.GetAttrPool().GetUserOrPoolDefaultItem(RES_PARATR_TABSTOP);
            NewAttr(aOrig);
        }
    }
}

/**
 * DOP
*/
void SwWW8ImplReader::ImportDop()
{
    // correct the LastPrinted date in DocumentProperties
    uno::Reference<document::XDocumentPropertiesSupplier> xDPS(
        m_pDocShell->GetModel(), uno::UNO_QUERY_THROW);
    uno::Reference<document::XDocumentProperties> xDocuProps(
        xDPS->getDocumentProperties());
    OSL_ENSURE(xDocuProps.is(), "DocumentProperties is null");
    if (xDocuProps.is())
    {
        DateTime aLastPrinted(
            msfilter::util::DTTM2DateTime(m_xWDop->dttmLastPrint));
        ::util::DateTime uDT = aLastPrinted.GetUNODateTime();
        xDocuProps->setPrintDate(uDT);
    }

    // COMPATIBILITY FLAGS START

    // #i78951# - remember the unknown compatibility options
    // so as to export them out
    m_rDoc.getIDocumentSettingAccess().Setn32DummyCompatibilityOptions1(m_xWDop->GetCompatibilityOptions());
    m_rDoc.getIDocumentSettingAccess().Setn32DummyCompatibilityOptions2(m_xWDop->GetCompatibilityOptions2());

    // The distance between two paragraphs is the sum of the bottom distance of
    // the first paragraph and the top distance of the second one
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::PARA_SPACE_MAX, m_xWDop->fDontUseHTMLAutoSpacing);
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::PARA_SPACE_MAX_AT_PAGES, true );
    // move tabs on alignment
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::TAB_COMPAT, true);
    // #i24363# tab stops relative to indent
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::TABS_RELATIVE_TO_INDENT, false);
    // tdf#117923
    m_rDoc.getIDocumentSettingAccess().set(
        DocumentSettingId::APPLY_PARAGRAPH_MARK_FORMAT_TO_NUMBERING, true);
    m_rDoc.getIDocumentSettingAccess().set(
        DocumentSettingId::MS_WORD_COMP_TRAILING_BLANKS, true);
    // tdf#128195
    m_rDoc.getIDocumentSettingAccess().set(
        DocumentSettingId::HEADER_SPACING_BELOW_LAST_PARA, true);
    m_rDoc.getIDocumentSettingAccess().set(
        DocumentSettingId::FRAME_AUTOWIDTH_WITH_MORE_PARA, true);
    m_rDoc.getIDocumentSettingAccess().set(
        DocumentSettingId::FOOTNOTE_IN_COLUMN_TO_PAGEEND, true);
    // tdf#155229 calculate minimum row height including horizontal border width
    m_rDoc.getIDocumentSettingAccess().set(
        DocumentSettingId::MIN_ROW_HEIGHT_INCL_BORDER, true);
    // use Word-compatible CJK text grid metrics
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::MS_WORD_COMP_GRID_METRICS, true);
    // tdf#167583
    m_rDoc.getIDocumentSettingAccess().set(
        DocumentSettingId::ADJUST_TABLE_LINE_HEIGHTS_TO_GRID_HEIGHT,
        !m_xWDop->fDontAdjustLineHeightInTable);

    // Import Default Tabs
    tools::Long nDefTabSiz = m_xWDop->dxaTab;
    if( nDefTabSiz < 56 )
        nDefTabSiz = 709;

    // We want exactly one DefaultTab
    SvxTabStopItem aNewTab( 1, sal_uInt16(nDefTabSiz), SvxTabAdjust::Default, RES_PARATR_TABSTOP );
    const_cast<SvxTabStop&>(aNewTab[0]).GetAdjustment() = SvxTabAdjust::Default;

    m_rDoc.GetAttrPool().SetUserDefaultItem( aNewTab );

    // Import zoom factor
    if (m_xWDop->wScaleSaved)
    {
        //Import zoom type
        sal_Int16 nZoomType;
        switch (m_xWDop->zkSaved) {
            case 1:  nZoomType = sal_Int16(SvxZoomType::WHOLEPAGE); break;
            case 2:  nZoomType = sal_Int16(SvxZoomType::PAGEWIDTH); break;
            case 3:  nZoomType = sal_Int16(SvxZoomType::OPTIMAL);   break;
            default: nZoomType = sal_Int16(SvxZoomType::PERCENT);   break;
        }
        uno::Sequence<beans::PropertyValue> aViewProps( comphelper::InitPropertySequence({
                { "ZoomFactor", uno::Any(sal_Int16(m_xWDop->wScaleSaved)) },
                { "VisibleBottom", uno::Any(sal_Int32(0)) },
                { "ZoomType", uno::Any(nZoomType) }
            }));

        rtl::Reference< comphelper::IndexedPropertyValuesContainer > xBox = new comphelper::IndexedPropertyValuesContainer();
        xBox->insertByIndex(sal_Int32(0), uno::Any(aViewProps));
        uno::Reference<document::XViewDataSupplier> xViewDataSupplier(m_pDocShell->GetModel(), uno::UNO_QUERY);
        xViewDataSupplier->setViewData(xBox);
    }

    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::USE_VIRTUAL_DEVICE, !m_xWDop->fUsePrinterMetrics);
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::USE_HIRES_VIRTUAL_DEVICE, true);
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::ADD_FLY_OFFSETS, true );

    // No vertical offsets would lead to e.g. overlap of table and fly frames.
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::ADD_VERTICAL_FLY_OFFSETS, true );

    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::ADD_EXT_LEADING, !m_xWDop->fNoLeading);
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::OLD_NUMBERING, false);
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::IGNORE_FIRST_LINE_INDENT_IN_NUMBERING, false); // #i47448#
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::NO_GAP_AFTER_NOTE_NUMBER, true); // tdf#159382
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::DO_NOT_JUSTIFY_LINES_WITH_MANUAL_BREAK, !m_xWDop->fExpShRtn); // #i49277#, #i56856#
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::BALANCE_SPACES_AND_IDEOGRAPHIC_SPACES,
                                           !m_xWDop->fDntBlnSbDbWid); // tdf#88908
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::DO_NOT_RESET_PARA_ATTRS_FOR_NUM_FONT, false);  // #i53199#
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::OLD_LINE_SPACING, false);

    // #i25901# - set new compatibility option
    //      'Add paragraph and table spacing at bottom of table cells'
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::ADD_PARA_SPACING_TO_TABLE_CELLS, true);
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::ADD_PARA_LINE_SPACING_TO_TABLE_CELLS, true);

    // #i11860# - set new compatibility option
    //      'Use former object positioning' to <false>
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::USE_FORMER_OBJECT_POS, false);

    // #i27767# - set new compatibility option
    //      'Consider Wrapping mode when positioning object' to <true>
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::CONSIDER_WRAP_ON_OBJECT_POSITION, true);

    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::USE_FORMER_TEXT_WRAPPING, false); // #i13832#, #i24135#

    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::TABLE_ROW_KEEP, true); //SetTableRowKeep( true );

    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::IGNORE_TABS_AND_BLANKS_FOR_LINE_CALCULATION, true); // #i3952#

    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::INVERT_BORDER_SPACING, true);
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::COLLAPSE_EMPTY_CELL_PARA, true);
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::UNBREAKABLE_NUMBERINGS, true);
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::CLIPPED_PICTURES, true);
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::TAB_OVER_MARGIN, true);
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::SURROUND_TEXT_WRAP_SMALL, true);
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::PROP_LINE_SPACING_SHRINKS_FIRST_LINE, true);
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::CONTINUOUS_ENDNOTES, true);
    // rely on default for HYPHENATE_URLS=false
    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::APPLY_PARAGRAPH_MARK_FORMAT_TO_EMPTY_LINE_AT_END_OF_PARAGRAPH, true);
    // rely on default for IGNORE_HIDDEN_CHARS_FOR_LINE_CALCULATION=true

    IDocumentSettingAccess& rIDSA = m_rDoc.getIDocumentSettingAccess();
    if (m_xWDop->fDontBreakWrappedTables)
    {
        rIDSA.set(DocumentSettingId::DO_NOT_BREAK_WRAPPED_TABLES, true);
    }

    if (m_xWDop->fDontVertAlignCellWithSp)
        rIDSA.set(DocumentSettingId::FORCE_TOP_ALIGNMENT_IN_CELL_WITH_FLOATING_ANCHOR, true);

    // COMPATIBILITY FLAGS END

    // Import magic doptypography information, if it's there
    if (m_xWwFib->m_nFib > 105)
        ImportDopTypography(m_xWDop->doptypography);

    // disable form design mode to be able to use imported controls directly
    // #i31239# always disable form design mode, not only in protected docs
    uno::Reference<beans::XPropertySet> xDocProps(m_pDocShell->GetModel(), uno::UNO_QUERY);
    if (xDocProps.is())
    {
        uno::Reference<beans::XPropertySetInfo> xInfo = xDocProps->getPropertySetInfo();
        if (xInfo.is())
        {
            if (xInfo->hasPropertyByName(u"ApplyFormDesignMode"_ustr))
                xDocProps->setPropertyValue(u"ApplyFormDesignMode"_ustr, css::uno::Any(false));
        }

        // for the benefit of DOCX - if this is ever saved in that format.
        comphelper::SequenceAsHashMap aGrabBag(xDocProps->getPropertyValue(u"InteropGrabBag"_ustr));
        uno::Sequence<beans::PropertyValue> aCompatSetting( comphelper::InitPropertySequence({
                { "name", uno::Any(u"compatibilityMode"_ustr) },
                { "uri", uno::Any(u"http://schemas.microsoft.com/office/word"_ustr) },
                { "val", uno::Any(u"11"_ustr) }  //11: Use features specified in MS-DOC.
        }));

        uno::Sequence< beans::PropertyValue > aValue(comphelper::InitPropertySequence({
            { "compatSetting", uno::Any(aCompatSetting) }
        }));

        aGrabBag[u"CompatSettings"_ustr] <<= aValue;
        xDocProps->setPropertyValue(u"InteropGrabBag"_ustr, uno::Any(aGrabBag.getAsConstPropertyValueList()));
    }

    // The password can force read-only, comments-only, fill-in-form-only, or require track-changes.
    // Treat comments-only like read-only since Writer has no support for that.
    // Still allow editing of form fields, without requiring the password.
    // Still allow editing if track-changes is locked on. (Currently LockRev is ignored/lost on export anyway.)
    if (!m_xWDop->fProtEnabled && !m_xWDop->fLockRev)
        m_pDocShell->SetModifyPasswordHash(m_xWDop->lKeyProtDoc);
    else if ( xDocProps.is() )
    {
        comphelper::SequenceAsHashMap aGrabBag(xDocProps->getPropertyValue(u"InteropGrabBag"_ustr));
        aGrabBag[u"FormPasswordHash"_ustr] <<= m_xWDop->lKeyProtDoc;
        xDocProps->setPropertyValue(u"InteropGrabBag"_ustr, uno::Any(aGrabBag.getAsConstPropertyValueList()));
    }

    if (officecfg::Office::Common::Filter::Microsoft::Import::ImportWWFieldsAsEnhancedFields::get())
        m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::PROTECT_FORM, m_xWDop->fProtEnabled );

    if (m_xWDop->iGutterPos)
    {
        m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::GUTTER_AT_TOP, true);
    }
}

void SwWW8ImplReader::ImportDopTypography(const WW8DopTypography &rTypo)
{
    switch (rTypo.m_iLevelOfKinsoku)
    {
        case 2: // custom
            {
                i18n::ForbiddenCharacters aForbidden(OUString(+rTypo.m_rgxchFPunct),
                    OUString(+rTypo.m_rgxchLPunct));
                    // unary + makes sure not to accidentally call the deleted
                    // OUString(ConstCharArrayDetector<...>::TypeUtf16) ctor that takes the full
                    // m_rgxchFPunct, m_rgxchLPunct arrays with embedded NULs, instead of just the
                    // prefix leading up to the first NUL
                m_rDoc.getIDocumentSettingAccess().setForbiddenCharacters(rTypo.GetConvertedLang(),
                        aForbidden);
                // Obviously cannot set the standard level 1 for japanese, so
                // bail out now while we can.
                if (rTypo.GetConvertedLang() == LANGUAGE_JAPANESE)
                    return;
            }
            break;
        default:
            break;
    }

    /*
    This MS hack means that level 2 of japanese is not in operation, so we put
    in what we know are the MS defaults, there is a complementary reverse
    hack in the writer. Its our default as well, but we can set it anyway
    as a flag for later.
    */
    if (!rTypo.m_reserved2)
    {
        i18n::ForbiddenCharacters aForbidden(WW8DopTypography::JapanNotBeginLevel1,
            WW8DopTypography::JapanNotEndLevel1);
        m_rDoc.getIDocumentSettingAccess().setForbiddenCharacters(LANGUAGE_JAPANESE,aForbidden);
    }

    m_rDoc.getIDocumentSettingAccess().set(DocumentSettingId::KERN_ASIAN_PUNCTUATION, bool(rTypo.m_fKerningPunct));
    m_rDoc.getIDocumentSettingAccess().setCharacterCompressionType(static_cast<CharCompressType>(rTypo.m_iJustification));
}

/**
 * Footnotes and Endnotes
 */
WW8ReaderSave::WW8ReaderSave(SwWW8ImplReader* pRdr ,WW8_CP nStartCp) :
    mxTmpPos(pRdr->m_rDoc.CreateUnoCursor(*pRdr->m_pPaM->GetPoint())),
    mxOldStck(std::move(pRdr->m_xCtrlStck)),
    mxOldAnchorStck(std::move(pRdr->m_xAnchorStck)),
    mxOldRedlines(std::move(pRdr->m_xRedlineStack)),
    mxOldPlcxMan(pRdr->m_xPlcxMan),
    mpWFlyPara(std::move(pRdr->m_xWFlyPara)),
    mpSFlyPara(std::move(pRdr->m_xSFlyPara)),
    mpPreviousNumPaM(pRdr->m_pPreviousNumPaM),
    mpPrevNumRule(pRdr->m_pPrevNumRule),
    mxTableDesc(std::move(pRdr->m_xTableDesc)),
    mnInTable(pRdr->m_nInTable),
    mnCurrentColl(pRdr->m_nCurrentColl),
    mcSymbol(pRdr->m_cSymbol),
    mbIgnoreText(pRdr->m_bIgnoreText),
    mbSymbol(pRdr->m_bSymbol),
    mbHdFtFootnoteEdn(pRdr->m_bHdFtFootnoteEdn),
    mbTxbxFlySection(pRdr->m_bTxbxFlySection),
    mbAnl(pRdr->m_bAnl),
    mbInHyperlink(pRdr->m_bInHyperlink),
    mbPgSecBreak(pRdr->m_bPgSecBreak),
    mbWasParaEnd(pRdr->m_bWasParaEnd),
    mbHasBorder(pRdr->m_bHasBorder),
    mbFirstPara(pRdr->m_bFirstPara)
{
    pRdr->m_bSymbol = false;
    pRdr->m_bHdFtFootnoteEdn = true;
    pRdr->m_bTxbxFlySection = pRdr->m_bAnl = pRdr->m_bPgSecBreak = pRdr->m_bWasParaEnd
        = pRdr->m_bHasBorder = false;
    pRdr->m_bFirstPara = true;
    pRdr->m_nInTable = 0;
    pRdr->m_pPreviousNumPaM = nullptr;
    pRdr->m_pPrevNumRule = nullptr;
    pRdr->m_nCurrentColl = 0;

    pRdr->m_xCtrlStck.reset(new SwWW8FltControlStack(pRdr->m_rDoc, pRdr->m_nFieldFlags,
        *pRdr));

    pRdr->m_xRedlineStack.reset(new sw::util::RedlineStack(pRdr->m_rDoc));

    pRdr->m_xAnchorStck.reset(new SwWW8FltAnchorStack(pRdr->m_rDoc, pRdr->m_nFieldFlags));

    // Save the attribute manager: we need this as the newly created PLCFx Manager
    // access the same FKPs as the old one and their Start-End position changes.
    if (pRdr->m_xPlcxMan)
        pRdr->m_xPlcxMan->SaveAllPLCFx(maPLCFxSave);

    if (nStartCp != -1)
    {
        pRdr->m_xPlcxMan = std::make_shared<WW8PLCFMan>(pRdr->m_xSBase.get(),
            mxOldPlcxMan->GetManType(), nStartCp);
    }

    maOldApos.push_back(false);
    maOldApos.swap(pRdr->m_aApos);
    maOldFieldStack.swap(pRdr->m_aFieldStack);
}

void WW8ReaderSave::Restore( SwWW8ImplReader* pRdr )
{
    pRdr->m_xWFlyPara = std::move(mpWFlyPara);
    pRdr->m_xSFlyPara = std::move(mpSFlyPara);
    pRdr->m_pPreviousNumPaM = mpPreviousNumPaM;
    pRdr->m_pPrevNumRule = mpPrevNumRule;
    pRdr->m_xTableDesc = std::move(mxTableDesc);
    pRdr->m_cSymbol = mcSymbol;
    pRdr->m_bSymbol = mbSymbol;
    pRdr->m_bIgnoreText = mbIgnoreText;
    pRdr->m_bHdFtFootnoteEdn = mbHdFtFootnoteEdn;
    pRdr->m_bTxbxFlySection = mbTxbxFlySection;
    pRdr->m_nInTable = mnInTable;
    pRdr->m_bAnl = mbAnl;
    pRdr->m_bInHyperlink = mbInHyperlink;
    pRdr->m_bWasParaEnd = mbWasParaEnd;
    pRdr->m_bPgSecBreak = mbPgSecBreak;
    pRdr->m_nCurrentColl = mnCurrentColl;
    pRdr->m_bHasBorder = mbHasBorder;
    pRdr->m_bFirstPara = mbFirstPara;

    // Close all attributes as attributes could be created that extend the Fly
    pRdr->DeleteCtrlStack();
    pRdr->m_xCtrlStck = std::move(mxOldStck);

    pRdr->m_xRedlineStack->closeall(*pRdr->m_pPaM->GetPoint());

    // ofz#37322 drop m_oLastAnchorPos during RedlineStack dtor and restore it afterwards to the same
    // place, or somewhere close if that place got destroyed
    std::shared_ptr<SwUnoCursor> xLastAnchorCursor(pRdr->m_oLastAnchorPos ? pRdr->m_rDoc.CreateUnoCursor(*pRdr->m_oLastAnchorPos) : nullptr);
    pRdr->m_oLastAnchorPos.reset();

    pRdr->m_xRedlineStack = std::move(mxOldRedlines);

    if (xLastAnchorCursor)
        pRdr->m_oLastAnchorPos.emplace(*xLastAnchorCursor->GetPoint());

    pRdr->DeleteAnchorStack();
    pRdr->m_xAnchorStck = std::move(mxOldAnchorStck);

    *pRdr->m_pPaM->GetPoint() = GetStartPos();

    if (mxOldPlcxMan != pRdr->m_xPlcxMan)
        pRdr->m_xPlcxMan = mxOldPlcxMan;
    if (pRdr->m_xPlcxMan)
        pRdr->m_xPlcxMan->RestoreAllPLCFx(maPLCFxSave);
    pRdr->m_aApos.swap(maOldApos);
    pRdr->m_aFieldStack.swap(maOldFieldStack);
}

void SwWW8ImplReader::Read_HdFtFootnoteText( const SwNodeIndex* pSttIdx,
    WW8_CP nStartCp, WW8_CP nLen, ManTypes nType )
{
    if (nStartCp < 0 || nLen < 0)
        return;

    // Saves Flags (amongst other things) and resets them
    WW8ReaderSave aSave( this );

    m_pPaM->GetPoint()->Assign( pSttIdx->GetIndex() + 1 );

    // Read Text for Header, Footer or Footnote
    ReadText( nStartCp, nLen, nType ); // Ignore Sepx when doing so
    aSave.Restore( this );
}

/**
 * Use authornames, if not available fall back to initials.
 */
tools::Long SwWW8ImplReader::Read_And(WW8PLCFManResult* pRes)
{
    WW8PLCFx_SubDoc* pSD = m_xPlcxMan->GetAtn();
    if (!pSD)
        return 0;

    const void* pData = pSD->GetData();
    if (!pData)
        return 0;

    OUString sAuthor;
    OUString sInitials;
    if( m_bVer67 )
    {
        const WW67_ATRD* pDescri = static_cast<const WW67_ATRD*>(pData);
        const OUString* pA = GetAnnotationAuthor(SVBT16ToUInt16(pDescri->ibst));
        if (pA)
            sAuthor = *pA;
        else
        {
            const sal_uInt8 nLen = std::min<sal_uInt8>(pDescri->xstUsrInitl[0],
                                                       SAL_N_ELEMENTS(pDescri->xstUsrInitl)-1);
            sAuthor = OUString(pDescri->xstUsrInitl + 1, nLen, RTL_TEXTENCODING_MS_1252);
        }
    }
    else
    {
        const WW8_ATRD* pDescri = static_cast<const WW8_ATRD*>(pData);
        {
            const sal_uInt16 nLen = std::min<sal_uInt16>(SVBT16ToUInt16(pDescri->xstUsrInitl[0]),
                                                         SAL_N_ELEMENTS(pDescri->xstUsrInitl)-1);
            OUStringBuffer aBuf;
            aBuf.setLength(nLen);
            for(sal_uInt16 nIdx = 1; nIdx <= nLen; ++nIdx)
                aBuf[nIdx-1] = SVBT16ToUInt16(pDescri->xstUsrInitl[nIdx]);
            sInitials = aBuf.makeStringAndClear();
        }

        if (const OUString* pA = GetAnnotationAuthor(SVBT16ToUInt16(pDescri->ibst)))
            sAuthor = *pA;
        else
            sAuthor = sInitials;
    }

    sal_uInt32 nDateTime = 0;

    if (sal_uInt8 * pExtended = m_xPlcxMan->GetExtendedAtrds()) // Word < 2002 has no date data for comments
    {
        sal_uLong nIndex = pSD->GetIdx() & 0xFFFF; // Index is (stupidly) multiplexed for WW8PLCFx_SubDocs
        if (m_xWwFib->m_lcbAtrdExtra/18 > nIndex)
            nDateTime = SVBT32ToUInt32(*reinterpret_cast<SVBT32*>(pExtended+(nIndex*18)));
    }

    DateTime aDate = msfilter::util::DTTM2DateTime(nDateTime);

    OUString sText;
    std::optional<OutlinerParaObject> pOutliner = ImportAsOutliner( sText, pRes->nCp2OrIdx,
        pRes->nCp2OrIdx + pRes->nMemLen, MAN_AND );

    m_xFormatOfJustInsertedApo.reset();
    SwPostItField aPostIt(
        static_cast<SwPostItFieldType*>(m_rDoc.getIDocumentFieldsAccess().GetSysFieldType(SwFieldIds::Postit)), sAuthor,
        sText, sInitials, SwMarkName(), aDate );
    aPostIt.SetTextObject(std::move(pOutliner));

    SwPaM aEnd(*m_pPaM->End(), *m_pPaM->End());
    m_xCtrlStck->NewAttr(*aEnd.GetPoint(), SvxCharHiddenItem(false, RES_CHRATR_HIDDEN));
    m_rDoc.getIDocumentContentOperations().InsertPoolItem(aEnd, SwFormatField(aPostIt));
    m_xCtrlStck->SetAttr(*aEnd.GetPoint(), RES_CHRATR_HIDDEN);
    // If this is a range, make sure that it ends after the just inserted character, not before it.
    m_xReffedStck->MoveAttrs(*aEnd.GetPoint(), SwFltControlStack::MoveAttrsMode::POSTIT_INSERTED);

    return 0;
}

void SwWW8ImplReader::Read_HdFtTextAsHackedFrame(WW8_CP nStart, WW8_CP nLen,
    SwFrameFormat const &rHdFtFormat, sal_uInt16 nPageWidth)
{
    const SwNodeIndex* pSttIdx = rHdFtFormat.GetContent().GetContentIdx();
    OSL_ENSURE(pSttIdx, "impossible");
    if (!pSttIdx)
        return;

    SwPosition aTmpPos(*m_pPaM->GetPoint());

    m_pPaM->GetPoint()->Assign( pSttIdx->GetIndex() + 1 );

    // tdf#122425: Explicitly remove borders and spacing
    SfxItemSetFixed<RES_FRMATR_BEGIN, RES_FRMATR_END - 1> aFlySet(m_rDoc.GetAttrPool());
    Reader::ResetFrameFormatAttrs(aFlySet);

    SwFlyFrameFormat* pFrame
        = m_rDoc.MakeFlySection(RndStdIds::FLY_AT_PARA, m_pPaM->GetPoint(), &aFlySet);

    SwFormatAnchor aAnch( pFrame->GetAnchor() );
    aAnch.SetType( RndStdIds::FLY_AT_PARA );
    pFrame->SetFormatAttr( aAnch );
    SwFormatFrameSize aSz(SwFrameSize::Minimum, nPageWidth, MINLAY);
    SwFrameSize eFrameSize = SwFrameSize::Minimum;
    if( eFrameSize != aSz.GetWidthSizeType() )
        aSz.SetWidthSizeType( eFrameSize );
    pFrame->SetFormatAttr(aSz);
    pFrame->SetFormatAttr(SwFormatSurround(css::text::WrapTextMode_THROUGH));
    pFrame->SetFormatAttr(SwFormatHoriOrient(0, text::HoriOrientation::LEFT)); //iFOO

    // #i43427# - send frame for header/footer into background.
    pFrame->SetFormatAttr( SvxOpaqueItem( RES_OPAQUE, false ) );
    SdrObject* pFrameObj = CreateContactObject( pFrame );
    OSL_ENSURE( pFrameObj,
            "<SwWW8ImplReader::Read_HdFtTextAsHackedFrame(..)> - missing SdrObject instance" );
    if ( pFrameObj )
    {
        pFrameObj->SetOrdNum( 0 );
    }
    MoveInsideFly(pFrame);

    const SwNodeIndex* pHackIdx = pFrame->GetContent().GetContentIdx();

    Read_HdFtFootnoteText(pHackIdx, nStart, nLen - 1, MAN_HDFT);

    MoveOutsideFly(pFrame, aTmpPos);
}

void SwWW8ImplReader::Read_HdFtText(WW8_CP nStart, WW8_CP nLen, SwFrameFormat const * pHdFtFormat)
{
    const SwNodeIndex* pSttIdx = pHdFtFormat->GetContent().GetContentIdx();
    if (!pSttIdx)
        return;

    SwPosition aTmpPos( *m_pPaM->GetPoint() ); // Remember old cursor position

    Read_HdFtFootnoteText(pSttIdx, nStart, nLen - 1, MAN_HDFT);

    *m_pPaM->GetPoint() = std::move(aTmpPos);
}

bool SwWW8ImplReader::isValid_HdFt_CP(WW8_CP nHeaderCP) const
{
    // Each CP of Plcfhdd MUST be less than FibRgLw97.ccpHdd
    return (nHeaderCP < m_xWwFib->m_ccpHdr && nHeaderCP >= 0);
}

bool SwWW8ImplReader::HasOwnHeaderFooter(sal_uInt8 nWhichItems, sal_uInt8 grpfIhdt,
    int nSect)
{
    if (m_xHdFt)
    {
        WW8_CP nStart, nLen;
        sal_uInt8 nNumber = 5;

        for( sal_uInt8 nI = 0x20; nI; nI >>= 1, nNumber-- )
        {
            if (nI & nWhichItems)
            {
                bool bOk = true;
                if( m_bVer67 )
                    bOk = ( m_xHdFt->GetTextPos(grpfIhdt, nI, nStart, nLen ) && nStart >= 0 && nLen >= 2 );
                else
                {
                    m_xHdFt->GetTextPosExact( static_cast< short >(nNumber + (nSect+1)*6), nStart, nLen);
                    bOk = ( 2 <= nLen ) && isValid_HdFt_CP(nStart);
                }

                if (bOk)
                    return true;
            }
        }
    }
    return false;
}

void SwWW8ImplReader::Read_HdFt(int nSect, const SwPageDesc *pPrev,
    const wwSection &rSection)
{
    sal_uInt8 grpfIhdt = rSection.maSep.grpfIhdt;
    SwPageDesc *pPD = rSection.mpPage;

    if( !m_xHdFt )
        return;

    WW8_CP nStart, nLen;
    sal_uInt8 nNumber = 5;

    // This loops through the 6 flags WW8_{FOOTER,HEADER}_{ODD,EVEN,FIRST}
    // corresponding to bit fields in grpfIhdt indicating which
    // header/footer(s) are present in this section
    for( sal_uInt8 nI = 0x20; nI; nI >>= 1, nNumber-- )
    {
        if (nI & grpfIhdt)
        {
            bool bOk = true;
            if( m_bVer67 )
                bOk = ( m_xHdFt->GetTextPos(grpfIhdt, nI, nStart, nLen ) && nLen >= 2 );
            else
            {
                m_xHdFt->GetTextPosExact( static_cast< short >(nNumber + (nSect+1)*6), nStart, nLen);
                bOk = ( 2 <= nLen ) && isValid_HdFt_CP(nStart);
            }

            bool bUseLeft
                = (nI & ( WW8_HEADER_EVEN | WW8_FOOTER_EVEN )) != 0;
            bool bUseFirst
                = (nI & ( WW8_HEADER_FIRST | WW8_FOOTER_FIRST )) != 0;

            // If we are loading a first-page header/footer which is not
            // actually enabled in this section (it still needs to be
            // loaded as it may be inherited by a later section)
            bool bDisabledFirst = bUseFirst && !rSection.HasTitlePage();

            bool bFooter
                = (nI & ( WW8_FOOTER_EVEN | WW8_FOOTER_ODD | WW8_FOOTER_FIRST )) != 0;

            SwFrameFormat& rFormat = bUseLeft ? pPD->GetLeft()
                : bUseFirst ? pPD->GetFirstMaster()
                : pPD->GetMaster();

            SwFrameFormat* pHdFtFormat;
            // If we have empty first page header and footer.
            bool bNoFirst = !(grpfIhdt & WW8_HEADER_FIRST) && !(grpfIhdt & WW8_FOOTER_FIRST);
            if (bFooter)
            {
                m_bIsFooter = true;
                //#i17196# Cannot have left without right
                if (!bDisabledFirst
                        && !pPD->GetMaster().GetFooter().GetFooterFormat())
                    pPD->GetMaster().SetFormatAttr(SwFormatFooter(true));
                if (bUseLeft)
                    pPD->GetLeft().SetFormatAttr(SwFormatFooter(true));
                if (bUseFirst || (rSection.maSep.fTitlePage && bNoFirst))
                    pPD->GetFirstMaster().SetFormatAttr(SwFormatFooter(true));
                pHdFtFormat = const_cast<SwFrameFormat*>(rFormat.GetFooter().GetFooterFormat());
            }
            else
            {
                m_bIsHeader = true;
                //#i17196# Cannot have left without right
                if (!bDisabledFirst
                        && !pPD->GetMaster().GetHeader().GetHeaderFormat())
                    pPD->GetMaster().SetFormatAttr(SwFormatHeader(true));
                if (bUseLeft)
                    pPD->GetLeft().SetFormatAttr(SwFormatHeader(true));
                if (bUseFirst || (rSection.maSep.fTitlePage && bNoFirst))
                    pPD->GetFirstMaster().SetFormatAttr(SwFormatHeader(true));
                pHdFtFormat = const_cast<SwFrameFormat*>(rFormat.GetHeader().GetHeaderFormat());
            }

            if (bOk)
            {
                bool bHackRequired = false;
                if (m_bIsHeader && rSection.IsFixedHeightHeader())
                    bHackRequired = true;
                else if (m_bIsFooter && rSection.IsFixedHeightFooter())
                    bHackRequired = true;

                if (bHackRequired)
                {
                    Read_HdFtTextAsHackedFrame(nStart, nLen, *pHdFtFormat,
                        static_cast< sal_uInt16 >(rSection.GetTextAreaWidth()) );
                }
                else
                    Read_HdFtText(nStart, nLen, pHdFtFormat);
            }
            else if (pPrev)
                CopyPageDescHdFt(pPrev, pPD, nI);

            m_bIsHeader = m_bIsFooter = false;
        }
    }
}

bool wwSectionManager::SectionIsProtected(const wwSection &rSection) const
{
    return ( mrReader.m_xWDop->fProtEnabled && !rSection.IsNotProtected() );
}

void wwSectionManager::SetHdFt(wwSection const &rSection, int nSect,
    const wwSection *pPrevious)
{
    // Header/Footer not present
    if (!rSection.maSep.grpfIhdt)
        return;

    OSL_ENSURE(rSection.mpPage, "makes no sense to call with a main page");
    if (rSection.mpPage)
    {
        mrReader.Read_HdFt(nSect, pPrevious ? pPrevious->mpPage : nullptr,
                rSection);
    }

    // Header/Footer - Update Index
    // So that the index is still valid later on
    if (mrReader.m_xHdFt)
        mrReader.m_xHdFt->UpdateIndex(rSection.maSep.grpfIhdt);

}

void SwWW8ImplReader::FinalizeTextNode(SwPosition& rPos, bool bAddNew)
{
    SwTextNode* pText = m_pPaM->GetPointNode().GetTextNode();

    const SwNumRule* pRule = nullptr;

    if (pText != nullptr)
        pRule = sw::util::GetNumRuleFromTextNode(*pText);

    // tdf#64222 / tdf#150613 filter out the "paragraph marker" formatting and
    // set it as a separate paragraph property, just like we do for DOCX.
    // This is only being used for numbering currently, so limiting to that context.
    if (pRule)
    {
        SfxItemSetFixed<RES_CHRATR_BEGIN, RES_CHRATR_END - 1, RES_TXTATR_CHARFMT,
                        RES_TXTATR_CHARFMT, RES_UNKNOWNATR_BEGIN, RES_UNKNOWNATR_END - 1>
            items(m_pPaM->GetDoc().GetAttrPool());

        SfxWhichIter aIter(items);
        for (sal_uInt16 nWhich = aIter.FirstWhich(); nWhich; nWhich = aIter.NextWhich())
        {
            const SfxPoolItem* pItem = m_xCtrlStck->GetStackAttr(rPos, nWhich);
            if (pItem)
                items.Put(*pItem);
        }
        SwFormatAutoFormat item(RES_PARATR_LIST_AUTOFMT);
        item.SetStyleHandle(std::make_shared<SfxItemSet>(items));
        pText->SetAttr(item);
    }

    if (
         pRule && !m_xWDop->fDontUseHTMLAutoSpacing &&
         (m_bParaAutoBefore || m_bParaAutoAfter)
       )
    {
        // If after spacing is set to auto, set the after space to 0
        if (m_bParaAutoAfter)
            SetLowerSpacing(*m_pPaM, 0);

        // If the previous textnode had numbering and
        // and before spacing is set to auto, set before space to 0
        if(m_pPrevNumRule && m_bParaAutoBefore)
            SetUpperSpacing(*m_pPaM, 0);

        // If the previous numbering rule was different we need
        // to insert a space after the previous paragraph
        if((pRule != m_pPrevNumRule) && m_pPreviousNumPaM)
            SetLowerSpacing(*m_pPreviousNumPaM, GetParagraphAutoSpace(m_xWDop->fDontUseHTMLAutoSpacing));

        // cache current paragraph
        if(m_pPreviousNumPaM)
        {
            delete m_pPreviousNumPaM;
            m_pPreviousNumPaM = nullptr;
        }

        m_pPreviousNumPaM = new SwPaM(*m_pPaM, m_pPaM);
        m_pPrevNumRule = pRule;
    }
    else if(!pRule && m_pPreviousNumPaM)
    {
        // If the previous paragraph has numbering but the current one does not
        // we need to add a space after the previous paragraph
        SetLowerSpacing(*m_pPreviousNumPaM, GetParagraphAutoSpace(m_xWDop->fDontUseHTMLAutoSpacing));
        delete m_pPreviousNumPaM;
        m_pPreviousNumPaM = nullptr;
        m_pPrevNumRule = nullptr;
    }
    else
    {
        // clear paragraph cache
        if(m_pPreviousNumPaM)
        {
            delete m_pPreviousNumPaM;
            m_pPreviousNumPaM = nullptr;
        }
        m_pPrevNumRule = pRule;
    }

    // If this is the first paragraph in the document and
    // Auto-spacing before paragraph is set,
    // set the upper spacing value to 0
    if(m_bParaAutoBefore && m_bFirstPara && !m_xWDop->fDontUseHTMLAutoSpacing)
        SetUpperSpacing(*m_pPaM, 0);

    m_bFirstPara = false;

    if (bAddNew)
        m_rDoc.getIDocumentContentOperations().AppendTextNode(rPos);

    // We can flush all anchored graphics at the end of a paragraph.
    m_xAnchorStck->Flush();
}

bool SwWW8ImplReader::SetSpacing(SwPaM &rMyPam, int nSpace, bool bIsUpper )
{
        bool bRet = false;
        const SwPosition* pSpacingPos = rMyPam.GetPoint();

        const SvxULSpaceItem* pULSpaceItem = m_xCtrlStck->GetFormatAttr(*pSpacingPos, RES_UL_SPACE);

        if(pULSpaceItem != nullptr)
        {
            SvxULSpaceItem aUL(*pULSpaceItem);

            if(bIsUpper)
                aUL.SetUpper( static_cast< sal_uInt16 >(nSpace) );
            else
                aUL.SetLower( static_cast< sal_uInt16 >(nSpace) );

            const sal_Int32 nEnd = pSpacingPos->GetContentIndex();
            rMyPam.GetPoint()->SetContent(0);
            m_xCtrlStck->NewAttr(*pSpacingPos, aUL);
            rMyPam.GetPoint()->SetContent(nEnd);
            m_xCtrlStck->SetAttr(*pSpacingPos, RES_UL_SPACE);
            bRet = true;
        }
        return bRet;
}

bool SwWW8ImplReader::SetLowerSpacing(SwPaM &rMyPam, int nSpace)
{
    return SetSpacing(rMyPam, nSpace, false);
}

bool SwWW8ImplReader::SetUpperSpacing(SwPaM &rMyPam, int nSpace)
{
    return SetSpacing(rMyPam, nSpace, true);
}

sal_uInt16 SwWW8ImplReader::TabRowSprm(int nLevel) const
{
    if (m_bVer67)
        return NS_sprm::v6::sprmPTtp;
    return nLevel ? NS_sprm::PFInnerTtp::val : NS_sprm::PFTtp::val;
}

void SwWW8ImplReader::EndSpecial()
{
    // Frame/Table/Anl
    if (m_bAnl)
        StopAllAnl(); // -> bAnl = false

    while(m_aApos.size() > 1)
    {
        StopTable();
        m_aApos.pop_back();
        --m_nInTable;
        if (m_aApos[m_nInTable])
            StopApo();
    }

    if (m_aApos[0])
        StopApo();

    OSL_ENSURE(!m_nInTable, "unclosed table!");
}

bool SwWW8ImplReader::ProcessSpecial(bool &rbReSync, WW8_CP nStartCp)
{
    // Frame/Table/Anl
    if (m_bInHyperlink)
        return false;

    rbReSync = false;

    OSL_ENSURE(m_nInTable >= 0,"nInTable < 0!");

    // TabRowEnd
    bool bTableRowEnd = (m_xPlcxMan->HasParaSprm(m_bVer67 ? 25 : 0x2417).pSprm != nullptr);

// Unfortunately, for every paragraph we need to check first whether
// they contain a sprm 29 (0x261B), which starts an APO.
// All other sprms then refer to that APO and not to the normal text
// surrounding it.
// The same holds true for a Table (sprm 24 (0x2416)) and Anls (sprm 13).

// WW: Table in APO is possible (Both Start-Ends occur at the same time)
// WW: APO in Table not possible

// This mean that of a Table is the content of an APO, the APO start needs
// to be edited first, so that the Table remains in the APO and not the
// other way around.
// At the End, however, we need to edit the Table End first as the APO
// must end after that Table (or else we never find the APO End).

// The same holds true for Fly / Anl, Tab / Anl, Fly / Tab / Anl.

// If the Table is within an APO the TabRowEnd Area misses the
// APO settings.
// To not end the APO there, we do not call ProcessApo

// KHZ: When there is a table inside the Apo the Apo-flags are also
//      missing for the 2nd, 3rd... paragraphs of each cell.

//  1st look for in-table flag, for 2000+ there is a subtable flag to
//  be considered, the sprm 6649 gives the level of the table
    sal_uInt8 nCellLevel = 0;

    if (m_bVer67)
        nCellLevel = int(nullptr != m_xPlcxMan->HasParaSprm(24).pSprm);
    else
    {
        nCellLevel = int(nullptr != m_xPlcxMan->HasParaSprm(0x2416).pSprm);
        if (!nCellLevel)
            nCellLevel = int(nullptr != m_xPlcxMan->HasParaSprm(0x244B).pSprm);
    }
    do
    {
        WW8_TablePos *pTabPos=nullptr;
        WW8_TablePos aTabPos;
        if(nCellLevel && !m_bVer67)
        {
            WW8PLCFxSave1 aSave;
            m_xPlcxMan->GetPap()->Save( aSave );
            rbReSync = true;
            WW8PLCFx_Cp_FKP* pPap = m_xPlcxMan->GetPapPLCF();
            WW8_CP nMyStartCp=nStartCp;

            SprmResult aLevel = m_xPlcxMan->HasParaSprm(0x6649);
            if (aLevel.pSprm && aLevel.nRemainingData >= 1)
                nCellLevel = *aLevel.pSprm;

            bool bHasRowEnd = SearchRowEnd(pPap, nMyStartCp, (m_nInTable<nCellLevel?m_nInTable:nCellLevel-1));

            // Bad Table, remain unchanged in level, e.g. #i19667#
            if (!bHasRowEnd)
                nCellLevel = static_cast< sal_uInt8 >(m_nInTable);

            if (bHasRowEnd && ParseTabPos(&aTabPos,pPap))
                pTabPos = &aTabPos;

            m_xPlcxMan->GetPap()->Restore( aSave );
        }

        // Then look if we are in an Apo

        ApoTestResults aApo = TestApo(nCellLevel, bTableRowEnd, pTabPos);

        // Look to see if we are in a Table, but Table in foot/end note not allowed
        bool bStartTab = (m_nInTable < nCellLevel) && !m_bFootnoteEdn;

        bool bStopTab = m_bWasTabRowEnd && (m_nInTable > nCellLevel) && !m_bFootnoteEdn;

        m_bWasTabRowEnd = false;  // must be deactivated right here to prevent next
                                // WW8TabDesc::TableCellEnd() from making nonsense

        if (m_nInTable && !bTableRowEnd && !bStopTab && (m_nInTable == nCellLevel && aApo.HasStartStop()))
            bStopTab = bStartTab = true; // Required to stop and start table

        //  Test for Anl (Numbering) and process all events in the right order
        if( m_bAnl && !bTableRowEnd )
        {
            SprmResult aSprm13 = m_xPlcxMan->HasParaSprm(13);
            const sal_uInt8* pSprm13 = aSprm13.pSprm;
            if (pSprm13 && aSprm13.nRemainingData >= 1)
            {   // Still Anl left?
                sal_uInt8 nT = static_cast< sal_uInt8 >(GetNumType( *pSprm13 ));
                if( ( nT != WW8_Pause && nT != m_nWwNumType ) // Anl change
                    || aApo.HasStartStop()                  // Forced Anl end
                    || bStopTab || bStartTab )
                {
                    StopAnlToRestart(nT);  // Anl-Restart (= change) over sprms
                }
                else
                {
                    NextAnlLine( pSprm13 ); // Next Anl Line
                }
            }
            else
            {   // Regular Anl end
                StopAllAnl(); // Actual end
            }
        }
        if (bStopTab)
        {
            StopTable();
            m_aApos.pop_back();
            --m_nInTable;
        }
        if (aApo.mbStopApo)
        {
            StopApo();
            m_aApos[m_nInTable] = false;
        }

        if (aApo.mbStartApo)
        {
            m_aApos[m_nInTable] = StartApo(aApo, pTabPos);
            // We need an ReSync after StartApo
            // (actually only if the Apo extends past a FKP border)
            rbReSync = true;
        }
        if (bStartTab)
        {
            WW8PLCFxSave1 aSave;
            m_xPlcxMan->GetPap()->Save( aSave );

            // Numbering for cell borders causes a crash -> no Anls in Tables
            if (m_bAnl)
               StopAllAnl();

            if(m_nInTable < nCellLevel)
            {
                if (StartTable(nStartCp))
                    ++m_nInTable;
                else
                    break;
                m_aApos.push_back(false);
            }

            if(m_nInTable >= nCellLevel)
            {
                // We need an ReSync after StartTable
                // (actually only if the Apo extends past a FKP border)
                rbReSync = true;
                m_xPlcxMan->GetPap()->Restore( aSave );
            }
        }
    } while (!m_bFootnoteEdn && (m_nInTable < nCellLevel));
    return bTableRowEnd;
}

rtl_TextEncoding SwWW8ImplReader::GetCharSetFromLanguage()
{
    /*
     #i22206#/#i52786#
     The (default) character set used for a run of text is the default
     character set for the version of Word that last saved the document.

     This is a bit tentative, more might be required if the concept is correct.
     When later version of word write older 6/95 documents the charset is
     correctly set in the character runs involved, so it's hard to reproduce
     documents that require this to be sure of the process involved.
    */
    const SvxLanguageItem *pLang = GetFormatAttr(RES_CHRATR_LANGUAGE);
    LanguageType eLang = pLang ? pLang->GetLanguage() : LANGUAGE_SYSTEM;
    css::lang::Locale aLocale(LanguageTag::convertToLocale(eLang));
    return msfilter::util::getBestTextEncodingFromLocale(aLocale);
}

rtl_TextEncoding SwWW8ImplReader::GetCJKCharSetFromLanguage()
{
    /*
     #i22206#/#i52786#
     The (default) character set used for a run of text is the default
     character set for the version of Word that last saved the document.

     This is a bit tentative, more might be required if the concept is correct.
     When later version of word write older 6/95 documents the charset is
     correctly set in the character runs involved, so it's hard to reproduce
     documents that require this to be sure of the process involved.
    */
    const SvxLanguageItem *pLang = GetFormatAttr(RES_CHRATR_CJK_LANGUAGE);
    LanguageType eLang = pLang ? pLang->GetLanguage() : LANGUAGE_SYSTEM;
    css::lang::Locale aLocale(LanguageTag::convertToLocale(eLang));
    return msfilter::util::getBestTextEncodingFromLocale(aLocale);
}

rtl_TextEncoding SwWW8ImplReader::GetCurrentCharSet()
{
    /*
    #i2015
    If the hard charset is set use it, if not see if there is an open
    character run that has set the charset, if not then fallback to the
    current underlying paragraph style.
    */
    rtl_TextEncoding eSrcCharSet = m_eHardCharSet;
    if (eSrcCharSet == RTL_TEXTENCODING_DONTKNOW)
    {
        if (!m_bVer67)
            eSrcCharSet = GetCharSetFromLanguage();
        else if (!m_aFontSrcCharSets.empty())
            eSrcCharSet = m_aFontSrcCharSets.top();
        if ((eSrcCharSet == RTL_TEXTENCODING_DONTKNOW) && m_nCharFormat >= 0 && o3tl::make_unsigned(m_nCharFormat) < m_vColl.size() )
            eSrcCharSet = m_vColl[m_nCharFormat].GetCharSet();
        if ((eSrcCharSet == RTL_TEXTENCODING_DONTKNOW) && StyleExists(m_nCurrentColl) && m_nCurrentColl < m_vColl.size())
            eSrcCharSet = m_vColl[m_nCurrentColl].GetCharSet();
        if (eSrcCharSet == RTL_TEXTENCODING_DONTKNOW)
            eSrcCharSet = GetCharSetFromLanguage();
    }
    return eSrcCharSet;
}

//Takashi Ono for CJK
rtl_TextEncoding SwWW8ImplReader::GetCurrentCJKCharSet()
{
    /*
    #i2015
    If the hard charset is set use it, if not see if there is an open
    character run that has set the charset, if not then fallback to the
    current underlying paragraph style.
    */
    rtl_TextEncoding eSrcCharSet = m_eHardCharSet;
    if (eSrcCharSet == RTL_TEXTENCODING_DONTKNOW)
    {
        if (!m_aFontSrcCJKCharSets.empty())
            eSrcCharSet = m_aFontSrcCJKCharSets.top();
        if ((eSrcCharSet == RTL_TEXTENCODING_DONTKNOW) && m_nCharFormat >= 0 && o3tl::make_unsigned(m_nCharFormat) < m_vColl.size() )
            eSrcCharSet = m_vColl[m_nCharFormat].GetCJKCharSet();
        if (eSrcCharSet == RTL_TEXTENCODING_DONTKNOW && StyleExists(m_nCurrentColl) && m_nCurrentColl < m_vColl.size())
            eSrcCharSet = m_vColl[m_nCurrentColl].GetCJKCharSet();
        if (eSrcCharSet == RTL_TEXTENCODING_DONTKNOW)
            eSrcCharSet = GetCJKCharSetFromLanguage();
    }
    return eSrcCharSet;
}

void SwWW8ImplReader::PostProcessAttrs()
{
    if (m_pPostProcessAttrsInfo == nullptr)
        return;

    SfxItemIter aIter(m_pPostProcessAttrsInfo->mItemSet);

    for (const SfxPoolItem* pItem = aIter.GetCurItem(); pItem; pItem = aIter.NextItem())
    {
        m_xCtrlStck->NewAttr(*m_pPostProcessAttrsInfo->mPaM.GetPoint(),
                           *pItem);
        m_xCtrlStck->SetAttr(*m_pPostProcessAttrsInfo->mPaM.GetMark(),
                           pItem->Which());
    }

    m_pPostProcessAttrsInfo.reset();
}

/*
 #i9240#
 It appears that some documents that are in a baltic 8 bit encoding which has
 some undefined characters can have use made of those characters, in which
 case they default to CP1252. If not then it's perhaps that the font encoding
 is only in use for 6/7 and for 8+ if we are in 8bit mode then the encoding
 is always 1252.

 So an encoding converter that on an undefined character attempts to
 convert from 1252 on the undefined character
*/
static std::size_t Custom8BitToUnicode(rtl_TextToUnicodeConverter hConverter,
    char const *pIn, std::size_t nInLen, sal_Unicode *pOut, std::size_t nOutLen)
{
    const sal_uInt32 nFlags =
        RTL_TEXTTOUNICODE_FLAGS_UNDEFINED_ERROR |
        RTL_TEXTTOUNICODE_FLAGS_MBUNDEFINED_ERROR |
        RTL_TEXTTOUNICODE_FLAGS_INVALID_IGNORE |
        RTL_TEXTTOUNICODE_FLAGS_FLUSH;

    const sal_uInt32 nFlags2 =
        RTL_TEXTTOUNICODE_FLAGS_UNDEFINED_IGNORE |
        RTL_TEXTTOUNICODE_FLAGS_MBUNDEFINED_IGNORE |
        RTL_TEXTTOUNICODE_FLAGS_INVALID_IGNORE |
        RTL_TEXTTOUNICODE_FLAGS_FLUSH;

    std::size_t nDestChars=0;
    std::size_t nConverted=0;

    do
    {
        sal_uInt32 nInfo = 0;
        sal_Size nThisConverted=0;

        nDestChars += rtl_convertTextToUnicode(hConverter, nullptr,
            pIn+nConverted, nInLen-nConverted,
            pOut+nDestChars, nOutLen-nDestChars,
            nFlags, &nInfo, &nThisConverted);

        OSL_ENSURE(nInfo == 0, "A character conversion failed!");

        nConverted += nThisConverted;

        if (
            nInfo & RTL_TEXTTOUNICODE_INFO_UNDEFINED ||
            nInfo & RTL_TEXTTOUNICODE_INFO_MBUNDEFINED
           )
        {
            sal_Size nOtherConverted;
            rtl_TextToUnicodeConverter hCP1252Converter =
                rtl_createTextToUnicodeConverter(RTL_TEXTENCODING_MS_1252);
            nDestChars += rtl_convertTextToUnicode(hCP1252Converter, nullptr,
                pIn+nConverted, 1,
                pOut+nDestChars, nOutLen-nDestChars,
                nFlags2, &nInfo, &nOtherConverted);
            rtl_destroyTextToUnicodeConverter(hCP1252Converter);
            nConverted+=1;
        }
    } while (nConverted < nInLen);

    return nDestChars;
}

bool SwWW8ImplReader::LangUsesHindiNumbers(LanguageType nLang)
{
    bool bResult = false;

    switch (static_cast<sal_uInt16>(nLang))
    {
        case 0x1401: // Arabic(Algeria)
        case 0x3c01: // Arabic(Bahrain)
        case 0xc01: // Arabic(Egypt)
        case 0x801: // Arabic(Iraq)
        case 0x2c01: // Arabic (Jordan)
        case 0x3401: // Arabic(Kuwait)
        case 0x3001: // Arabic(Lebanon)
        case 0x1001: // Arabic(Libya)
        case 0x1801: // Arabic(Morocco)
        case 0x2001: // Arabic(Oman)
        case 0x4001: // Arabic(Qatar)
        case 0x401: // Arabic(Saudi Arabia)
        case 0x2801: // Arabic(Syria)
        case 0x1c01: // Arabic(Tunisia)
        case 0x3801: // Arabic(U.A.E)
        case 0x2401: // Arabic(Yemen)
            bResult = true;
            break;
        default:
            break;
    }

    return bResult;
}

sal_Unicode SwWW8ImplReader::TranslateToHindiNumbers(sal_Unicode nChar)
{
    if (nChar >= 0x0030 && nChar <= 0x0039)
        return nChar + 0x0630;

    return nChar;
}

namespace
{
    OUString makeOUString(rtl_uString *pStr, sal_Int32 nAllocLen)
    {
        //if read len was in or around that of allocated len, just reuse pStr
        if (nAllocLen < pStr->length + 256)
            return OUString(pStr, SAL_NO_ACQUIRE);
        //otherwise copy the shorter used section to release extra mem
        OUString sRet(pStr->buffer, pStr->length);
        rtl_uString_release(pStr);
        return sRet;
    }
}

/**
 * Return value: true for non special chars
 */
bool SwWW8ImplReader::ReadPlainChars(WW8_CP& rPos, sal_Int32 nEnd, sal_Int32 nCpOfs)
{
    sal_Int32 nRequestedStrLen = nEnd - rPos;

    OSL_ENSURE(nRequestedStrLen, "String is 0");
    if (nRequestedStrLen <= 0)
        return true;

    WW8_CP nCp;
    const bool bFail = o3tl::checked_add(nCpOfs, rPos, nCp);
    if (bFail)
    {
        rPos+=nRequestedStrLen;
        return true;
    }

    sal_Int32 nRequestedPos = m_xSBase->WW8Cp2Fc(nCp, &m_bIsUnicode);
    bool bValidPos = checkSeek(*m_pStrm, nRequestedPos);
    OSL_ENSURE(bValidPos, "Document claimed to have more text than available");
    if (!bValidPos)
    {
        // Swallow missing range, e.g. #i95550#
        rPos+=nRequestedStrLen;
        return true;
    }

    std::size_t nAvailableStrLen = m_pStrm->remainingSize() / (m_bIsUnicode ? 2 : 1);
    OSL_ENSURE(nAvailableStrLen, "Document claimed to have more text than available");
    if (!nAvailableStrLen)
    {
        // Swallow missing range, e.g. #i95550#
        rPos+=nRequestedStrLen;
        return true;
    }

    sal_Int32 nValidStrLen = std::min<std::size_t>(nRequestedStrLen, nAvailableStrLen);

    // Reset Unicode flag and correct FilePos if needed.
    // Note: Seek is not expensive, as we're checking inline whether or not
    // the correct FilePos has already been reached.
    const sal_Int32 nStrLen = std::min(nValidStrLen, SAL_MAX_INT32-1);

    rtl_TextEncoding eSrcCharSet = m_bVer67 ? GetCurrentCharSet() :
        RTL_TEXTENCODING_MS_1252;
    if (m_bVer67 && eSrcCharSet == RTL_TEXTENCODING_MS_932)
    {
        /*
         fdo#82904

         Older documents exported as word 95 that use unicode aware fonts will
         have the charset of those fonts set to RTL_TEXTENCODING_MS_932 on
         export as the conversion from RTL_TEXTENCODING_UNICODE. This is a serious
         pain.

         We will try and use a fallback encoding if the conversion from
         RTL_TEXTENCODING_MS_932 fails, but you can get unlucky and get a document
         which isn't really in RTL_TEXTENCODING_MS_932 but parts of it form
         valid RTL_TEXTENCODING_MS_932 by chance :-(

         We're not the only ones that struggle with this: Here's the help from
         MSOffice 2003 on the topic:

         <<
          Earlier versions of Microsoft Word were sometimes used in conjunction with
          third-party language-processing add-in programs designed to support Chinese or
          Korean on English versions of Microsoft Windows. Use of these add-ins sometimes
          results in incorrect text display in more recent versions of Word.

          However, you can set options to convert these documents so that text is
          displayed correctly. On the Tools menu, click Options, and then click the
          General tab. In the English Word 6.0/95 documents list, select Contain Asian
          text (to have Word interpret the text as Asian code page data, regardless of
          its font) or Automatically detect Asian text (to have Word attempt to determine
          which parts of the text are meant to be Asian).
        >>

        What we can try here is to ignore a RTL_TEXTENCODING_MS_932 codepage if
        the language is not Japanese
        */

        const SvxLanguageItem * pItem = GetFormatAttr(RES_CHRATR_CJK_LANGUAGE);
        if (pItem != nullptr && LANGUAGE_JAPANESE != pItem->GetLanguage())
        {
            SAL_WARN("sw.ww8", "discarding word95 RTL_TEXTENCODING_MS_932 encoding");
            eSrcCharSet = GetCharSetFromLanguage();
        }
    }
    const rtl_TextEncoding eSrcCJKCharSet = m_bVer67 ? GetCurrentCJKCharSet() :
        RTL_TEXTENCODING_MS_1252;

    // allocate unicode string data
    auto l = [](rtl_uString* p){rtl_uString_release(p);};
    std::unique_ptr<rtl_uString, decltype(l)> xStr(rtl_uString_alloc(nStrLen), l);
    sal_Unicode* pBuffer = xStr->buffer;
    sal_Unicode* pWork = pBuffer;

    std::unique_ptr<char[]> p8Bits;

    rtl_TextToUnicodeConverter hConverter = nullptr;
    if (!m_bIsUnicode || m_bVer67)
        hConverter = rtl_createTextToUnicodeConverter(eSrcCharSet);

    if (!m_bIsUnicode)
        p8Bits.reset( new char[nStrLen] );

    // read the stream data
    sal_uInt8   nBCode = 0;
    sal_uInt16 nUCode;

    LanguageType nCTLLang = LANGUAGE_SYSTEM;
    const SvxLanguageItem * pItem = GetFormatAttr(RES_CHRATR_CTL_LANGUAGE);
    if (pItem != nullptr)
        nCTLLang = pItem->GetLanguage();

    sal_Int32 nL2;
    for (nL2 = 0; nL2 < nStrLen; ++nL2)
    {
        if (m_bIsUnicode)
            m_pStrm->ReadUInt16( nUCode ); // unicode  --> read 2 bytes
        else
        {
            m_pStrm->ReadUChar( nBCode ); // old code --> read 1 byte
            nUCode = nBCode;
        }

        if (!m_pStrm->good())
        {
            rPos = WW8_CP_MAX-10; // -> eof or other error
            return true;
        }

        if ((32 > nUCode) || (0xa0 == nUCode))
        {
            m_pStrm->SeekRel( m_bIsUnicode ? -2 : -1 );
            break; // Special character < 32, == 0xa0 found
        }

        if (m_bIsUnicode)
        {
            if (!m_bVer67)
                *pWork++ = nUCode;
            else
            {
                if (nUCode >= 0x3000) //0x8000 ?
                {
                    char aTest[2];
                    aTest[0] = static_cast< char >((nUCode & 0xFF00) >> 8);
                    aTest[1] = static_cast< char >(nUCode & 0x00FF);
                    OUString aTemp(aTest, 2, eSrcCJKCharSet);
                    OSL_ENSURE(aTemp.getLength() == 1, "so much for that theory");
                    *pWork++ = aTemp[0];
                }
                else
                {
                    char cTest = static_cast< char >(nUCode & 0x00FF);
                    pWork += Custom8BitToUnicode(hConverter, &cTest, 1, pWork, 1);
                }
            }
        }
        else
            p8Bits[nL2] = nBCode;
    }

    if (nL2)
    {
        const sal_Int32 nEndUsed = !m_bIsUnicode
            ? Custom8BitToUnicode(hConverter, p8Bits.get(), nL2, pBuffer, nStrLen)
            : pWork - pBuffer;

        if (m_bRegardHindiDigits && m_bBidi && LangUsesHindiNumbers(nCTLLang))
        {
            for (sal_Int32 nI = 0; nI < nEndUsed; ++nI, ++pBuffer)
                *pBuffer = TranslateToHindiNumbers(*pBuffer);
        }

        xStr->buffer[nEndUsed] = 0;
        xStr->length = nEndUsed;

        simpleAddTextToParagraph(makeOUString(xStr.release(), nStrLen));
        rPos += nL2;
        if (!m_aApos.back()) // a para end in apo doesn't count
            m_bWasParaEnd = false; // No CR
    }

    if (hConverter)
        rtl_destroyTextToUnicodeConverter(hConverter);
    return nL2 >= nStrLen;
}

namespace sw {

auto FilterControlChars(std::u16string_view aString) -> OUString
{
    OUStringBuffer buf(aString.size());
    for (size_t i = 0; i < aString.size(); ++i)
    {
        sal_Unicode const ch(aString[i]);
        if (!linguistic::IsControlChar(ch) || ch == '\r' || ch == '\n' || ch == '\t')
        {
            buf.append(ch);
        }
        else
        {
            SAL_INFO("sw.ww8", "filtering control character");
        }
    }
    return buf.makeStringAndClear();
}

} // namespace sw

void SwWW8ImplReader::simpleAddTextToParagraph(std::u16string_view aAddString)
{
    OUString const addString(sw::FilterControlChars(aAddString));

    if (addString.isEmpty())
        return;

    const SwContentNode *pCntNd = m_pPaM->GetPointContentNode();
    const SwTextNode* pNd = pCntNd ? pCntNd->GetTextNode() : nullptr;

    OSL_ENSURE(pNd, "What the hell, where's my text node");

    if (!pNd)
        return;

    const sal_Int32 nCharsLeft = SAL_MAX_INT32 - pNd->GetText().getLength();
    if (nCharsLeft > 0)
    {
        if (addString.getLength() <= nCharsLeft)
        {
            m_rDoc.getIDocumentContentOperations().InsertString(*m_pPaM, addString);
        }
        else
        {
            m_rDoc.getIDocumentContentOperations().InsertString(*m_pPaM, addString.copy(0, nCharsLeft));
            FinalizeTextNode(*m_pPaM->GetPoint());
            m_rDoc.getIDocumentContentOperations().InsertString(*m_pPaM, addString.copy(nCharsLeft));
        }
    }
    else
    {
        FinalizeTextNode(*m_pPaM->GetPoint());
        m_rDoc.getIDocumentContentOperations().InsertString(*m_pPaM, addString);
    }

    m_bReadTable = false;
}

/**
 * Return value: true for para end
 */
bool SwWW8ImplReader::ReadChars(WW8_CP& rPos, WW8_CP nNextAttr, tools::Long nTextEnd,
    tools::Long nCpOfs)
{
    tools::Long nEnd = ( nNextAttr < nTextEnd ) ? nNextAttr : nTextEnd;

    if (m_bSymbol || m_bIgnoreText)
    {
        WW8_CP nRequested = nEnd - rPos;
        if (m_bSymbol) // Insert special chars
        {
            sal_uInt64 nMaxPossible = m_pStrm->remainingSize();
            if (o3tl::make_unsigned(nRequested) > nMaxPossible)
            {
                SAL_WARN("sw.ww8", "document claims to have more characters, " << nRequested << " than remaining, " << nMaxPossible);
                nRequested = nMaxPossible;
            }

            if (!linguistic::IsControlChar(m_cSymbol)
                || m_cSymbol == '\r' || m_cSymbol == '\n' || m_cSymbol == '\t')
            {
                for (WW8_CP nCh = 0; nCh < nRequested; ++nCh)
                {
                    m_rDoc.getIDocumentContentOperations().InsertString(*m_pPaM, OUString(m_cSymbol));
                }
                m_xCtrlStck->SetAttr(*m_pPaM->GetPoint(), RES_CHRATR_FONT);
                m_xCtrlStck->SetAttr(*m_pPaM->GetPoint(), RES_CHRATR_CJK_FONT);
                m_xCtrlStck->SetAttr(*m_pPaM->GetPoint(), RES_CHRATR_CTL_FONT);
            }
        }
        m_pStrm->SeekRel(nRequested);
        rPos = nEnd; // Ignore until attribute end
        return false;
    }

    while (true)
    {
        if (ReadPlainChars(rPos, nEnd, nCpOfs))
            return false; // Done

        bool bStartLine = ReadChar(rPos, nCpOfs);
        rPos++;
        if (m_bPgSecBreak || bStartLine || rPos == nEnd) // CR or Done
        {
            return bStartLine;
        }
    }
}

bool SwWW8ImplReader::HandlePageBreakChar()
{
    bool bParaEndAdded = false;
    // #i1909# section/page breaks should not occur in tables, word
    // itself ignores them in this case.
    if (!m_nInTable)
    {
        bool IsTemp=true;
        SwTextNode* pTemp = m_pPaM->GetPointNode().GetTextNode();
        if (pTemp && pTemp->GetText().isEmpty()
                && (m_bFirstPara || m_bFirstParaOfPage))
        {
            IsTemp = false;
            FinalizeTextNode(*m_pPaM->GetPoint());
            pTemp->SetAttr(*GetDfltAttr(RES_PARATR_NUMRULE));
        }

        m_bPgSecBreak = true;
        m_xCtrlStck->KillUnlockedAttrs(*m_pPaM->GetPoint());
        /*
        If it's a 0x0c without a paragraph end before it, act like a
        paragraph end, but nevertheless, numbering (and perhaps other
        similar constructs) do not exist on the para.
        */
        if (!m_bWasParaEnd && IsTemp)
        {
            bParaEndAdded = true;
            if (0 >= m_pPaM->GetPoint()->GetContentIndex())
            {
                if (SwTextNode* pTextNode = m_pPaM->GetPointNode().GetTextNode())
                {
                    pTextNode->SetAttr(
                        *GetDfltAttr(RES_PARATR_NUMRULE));
                }
            }
        }
    }
    return bParaEndAdded;
}

bool SwWW8ImplReader::ReadChar(tools::Long nPosCp, tools::Long nCpOfs)
{
    bool bNewParaEnd = false;
    // Reset Unicode flag and correct FilePos if needed.
    // Note: Seek is not expensive, as we're checking inline whether or not
    // the correct FilePos has already been reached.
    std::size_t nRequestedPos = m_xSBase->WW8Cp2Fc(nCpOfs+nPosCp, &m_bIsUnicode);
    if (!checkSeek(*m_pStrm, nRequestedPos))
        return false;

    sal_uInt16 nWCharVal(0);
    if( m_bIsUnicode )
        m_pStrm->ReadUInt16( nWCharVal ); // unicode  --> read 2 bytes
    else
    {
        sal_uInt8 nBCode(0);
        m_pStrm -> ReadUChar( nBCode ); // old code --> read 1 byte
        nWCharVal = nBCode;
    }

    sal_Unicode cInsert = '\x0';
    bool bParaMark = false;

    if ( 0xc != nWCharVal )
        m_bFirstParaOfPage = false;

    switch (nWCharVal)
    {
        case 0:
            if (!m_bFuzzing)
            {
                // Page number
                SwPageNumberField aField(
                    static_cast<SwPageNumberFieldType*>(m_rDoc.getIDocumentFieldsAccess().GetSysFieldType(
                    SwFieldIds::PageNumber )), SwPageNumSubType::Random, SVX_NUM_ARABIC);
                m_rDoc.getIDocumentContentOperations().InsertPoolItem(*m_pPaM, SwFormatField(aField));
            }
            else
            {
                // extremely slow, so skip for fuzzing, and insert a space instead
                cInsert = ' ';
            }
            break;
        case 0xe:
            // if there is only one column word treats a column break like a pagebreak.
            if (m_aSectionManager.CurrentSectionColCount() < 2)
                bParaMark = HandlePageBreakChar();
            else if (!m_nInTable)
            {
                // Always insert a txtnode for a column break, e.g. ##
                SwContentNode *pCntNd=m_pPaM->GetPointContentNode();
                if (pCntNd!=nullptr && pCntNd->Len()>0) // if par is empty not break is needed
                    FinalizeTextNode(*m_pPaM->GetPoint());
                m_rDoc.getIDocumentContentOperations().InsertPoolItem(*m_pPaM, SvxFormatBreakItem(SvxBreak::ColumnBefore, RES_BREAK));
            }
            break;
        case 0x7:
            {
                bNewParaEnd = true;
                WW8PLCFxDesc* pPap = m_xPlcxMan->GetPap();
                //The last paragraph of each cell is terminated by a special
                //paragraph mark called a cell mark. Following the cell mark
                //that ends the last cell of a table row, the table row is
                //terminated by a special paragraph mark called a row mark
                //
                //So the 0x7 should be right at the end of the previous
                //range to be a real cell-end.
                if (pPap->nOrigStartPos == nPosCp+1 ||
                    pPap->nOrigStartPos == WW8_CP_MAX)
                {
                    TabCellEnd();       // Table cell/row end
                }
                else
                    bParaMark = true;
            }
            break;
        case 0xf:
            if( !m_bSpec )        // "Satellite"
                cInsert = u'\x00a4';
            break;
        case 0x14:
            if( !m_bSpec )        // "Para End" char
                cInsert = u'\x00b5';
                    //TODO: should this be U+00B6 PILCROW SIGN rather than
                    // U+00B5 MICRO SIGN?
            break;
        case 0x15:
            if( !m_bSpec )        // Juristenparagraph
            {
                cp_set::iterator aItr = m_aTOXEndCps.find(static_cast<WW8_CP>(nPosCp));
                if (aItr == m_aTOXEndCps.end())
                    cInsert = u'\x00a7';
                else
                    m_aTOXEndCps.erase(aItr);
            }
            break;
        case 0x9:
            cInsert = '\x9';    // Tab
            break;
        case 0xb:
            cInsert = '\xa';    // Hard NewLine
            break;
        case 0xc:
            bParaMark = HandlePageBreakChar();
            break;
        case 0x1e:              // Non-breaking hyphen
            m_rDoc.getIDocumentContentOperations().InsertString( *m_pPaM, OUString(CHAR_HARDHYPHEN) );
            break;
        case 0x1f:              // Non-required hyphens
            m_rDoc.getIDocumentContentOperations().InsertString( *m_pPaM, OUString(CHAR_SOFTHYPHEN) );
            break;
        case 0xa0:              // Non-breaking spaces
            m_rDoc.getIDocumentContentOperations().InsertString( *m_pPaM, OUString(CHAR_HARDBLANK)  );
            break;
        case 0x1:
            /*
            Current thinking is that if bObj is set then we have a
            straightforward "traditional" ole object, otherwise we have a
            graphic preview of an associated ole2 object (or a simple
            graphic of course)

            normally in the canvas field, the code is 0x8 0x1.
            in a special case, the code is 0x1 0x1, which yields a simple picture
            */
            {
                bool bReadObj = IsInlineEscherHack();
                if( bReadObj )
                {
                    sal_uInt64 nCurPos = m_pStrm->Tell();
                    sal_uInt16 nWordCode(0);

                    if( m_bIsUnicode )
                        m_pStrm->ReadUInt16( nWordCode );
                    else
                    {
                        sal_uInt8 nByteCode(0);
                        m_pStrm->ReadUChar( nByteCode );
                        nWordCode = nByteCode;
                    }
                    if( nWordCode == 0x1 )
                        bReadObj = false;
                    m_pStrm->Seek( nCurPos );
                }
                if( !bReadObj )
                {
                    SwFrameFormat *pResult = nullptr;
                    if (m_bObj)
                        pResult = ImportOle();
                    else if (m_bSpec)
                    {
                        SwFrameFormat* pAsCharFlyFormat =
                            m_rDoc.MakeFrameFormat(UIName(), m_rDoc.GetDfltFrameFormat(), true);
                        SwFormatAnchor aAnchor(RndStdIds::FLY_AS_CHAR);
                        pAsCharFlyFormat->SetFormatAttr(aAnchor);
                        pResult = ImportGraf(nullptr, pAsCharFlyFormat);
                        m_rDoc.DelFrameFormat(pAsCharFlyFormat);
                    }


                    // If we have a bad 0x1 insert a space instead.
                    if (!pResult)
                    {
                        cInsert = ' ';
                        OSL_ENSURE(!m_bObj && !m_bEmbeddObj && !m_nObjLocFc,
                            "WW8: Please report this document, it may have a "
                            "missing graphic");
                    }
                    else
                    {
                        // reset the flags.
                        m_bObj = m_bEmbeddObj = false;
                        m_nObjLocFc = 0;
                    }
                }
            }
            break;
        case 0x8:
            if( !m_bObj )
                Read_GrafLayer( nPosCp );
            break;
        case 0xd:
            bNewParaEnd = bParaMark = true;
            if (m_nInTable > 1)
            {
                /*
                #i9666#/#i23161#
                Yes complex, if there is an entry in the undocumented PLCF
                which I believe to be a record of cell and row boundaries
                see if the magic bit which I believe to mean cell end is
                set. I also think btw that the third byte of the 4 byte
                value is the level of the cell
                */
                WW8PLCFspecial* pTest = m_xPlcxMan->GetMagicTables();
                if (pTest && pTest->SeekPosExact(nPosCp+1+nCpOfs) &&
                    pTest->Where() == nPosCp+1+nCpOfs)
                {
                    WW8_FC nPos;
                    void *pData;
                    sal_uInt32 nData = pTest->Get(nPos, pData) ? SVBT32ToUInt32(*static_cast<SVBT32*>(pData))
                                                               : 0;
                    if (nData & 0x2) // Might be how it works
                    {
                        TabCellEnd();
                        bParaMark = false;
                    }
                }
                // tdf#106799: We expect TTP marks to be also cell marks,
                // but sometimes sprmPFInnerTtp comes without sprmPFInnerTableCell
                else if (m_bWasTabCellEnd || m_bWasTabRowEnd)
                {
                    TabCellEnd();
                    bParaMark = false;
                }
            }

            m_bWasTabCellEnd = false;

            break;              // line end
        case 0x5:               // Annotation reference
        case 0x13:
            break;
        case 0x2:               // TODO: Auto-Footnote-Number, should be replaced by SwWW8ImplReader::End_Footnote later
            if (!m_aFootnoteStack.empty())
                cInsert = '?';
            break;
        default:
            SAL_INFO( "sw.ww8.level2", "<unknownValue val=\"" << nWCharVal << "\">" );
            break;
    }

    if( '\x0' != cInsert )
    {
        OUString sInsert(cInsert);
        simpleAddTextToParagraph(sInsert);
    }
    if (!m_aApos.back()) // a para end in apo doesn't count
        m_bWasParaEnd = bNewParaEnd;
    return bParaMark;
}

void SwWW8ImplReader::ProcessCurrentCollChange(WW8PLCFManResult& rRes,
    bool* pStartAttr, bool bCallProcessSpecial)
{
    sal_uInt16 nOldColl = m_nCurrentColl;
    m_nCurrentColl = m_xPlcxMan->GetColl();

    // Invalid Style-Id
    if (m_nCurrentColl >= m_vColl.size() || !m_vColl[m_nCurrentColl].m_pFormat || !m_vColl[m_nCurrentColl].m_bColl)
    {
        m_nCurrentColl = 0;
        m_bParaAutoBefore = false;
        m_bParaAutoAfter = false;
    }
    else
    {
        m_bParaAutoBefore = m_vColl[m_nCurrentColl].m_bParaAutoBefore;
        m_bParaAutoAfter = m_vColl[m_nCurrentColl].m_bParaAutoAfter;
    }

    if (nOldColl >= m_vColl.size())
        nOldColl = 0; // guess! TODO make sure this is what we want

    bool bTabRowEnd = false;
    if( pStartAttr && bCallProcessSpecial && !m_bInHyperlink )
    {
        bool bReSync;
        // Frame/Table/Autonumbering List Level
        bTabRowEnd = ProcessSpecial(bReSync, rRes.nCurrentCp + m_xPlcxMan->GetCpOfs());
        if( bReSync )
            *pStartAttr = m_xPlcxMan->Get( &rRes ); // Get Attribute-Pos again
    }

    if (!bTabRowEnd && StyleExists(m_nCurrentColl))
    {
        SetTextFormatCollAndListLevel( *m_pPaM, m_vColl[ m_nCurrentColl ]);
        ChkToggleAttr(m_vColl[ nOldColl ].m_n81Flags, m_vColl[ m_nCurrentColl ].m_n81Flags);
        ChkToggleBiDiAttr(m_vColl[nOldColl].m_n81BiDiFlags,
            m_vColl[m_nCurrentColl].m_n81BiDiFlags);
    }
}

tools::Long SwWW8ImplReader::ReadTextAttr(WW8_CP& rTextPos, tools::Long nTextEnd, bool& rbStartLine, int nDepthGuard)
{
    tools::Long nSkipChars = 0;
    WW8PLCFManResult aRes;

    OSL_ENSURE(m_pPaM->GetPointNode().GetTextNode(), "Missing txtnode");
    bool bStartAttr = m_xPlcxMan->Get(&aRes); // Get Attribute position again
    aRes.nCurrentCp = rTextPos;                  // Current Cp position

    bool bNewSection = (aRes.nFlags & MAN_MASK_NEW_SEP) && !m_bIgnoreText;
    if ( bNewSection ) // New Section
    {
        OSL_ENSURE(m_pPaM->GetPointNode().GetTextNode(), "Missing txtnode");
        // Create PageDesc and fill it
        m_aSectionManager.CreateSep(rTextPos);
        // -> 0xc was a Sectionbreak, but not a Pagebreak;
        // Create PageDesc and fill it
        m_bPgSecBreak = false;
        OSL_ENSURE(m_pPaM->GetPointNode().GetTextNode(), "Missing txtnode");
    }

    // New paragraph over Plcx.Fkp.papx
    if ( (aRes.nFlags & MAN_MASK_NEW_PAP)|| rbStartLine )
    {
        ProcessCurrentCollChange( aRes, &bStartAttr,
            MAN_MASK_NEW_PAP == (aRes.nFlags & MAN_MASK_NEW_PAP) &&
            !m_bIgnoreText );
        rbStartLine = false;
    }

    // position of last CP that's to be ignored
    tools::Long nSkipPos = -1;

    if( 0 < aRes.nSprmId ) // Ignore empty Attrs
    {
        if( ( eFTN > aRes.nSprmId ) || ( 0x0800 <= aRes.nSprmId ) )
        {
            if( bStartAttr ) // WW attributes
            {
                if( aRes.nMemLen >= 0 )
                    ImportSprm(aRes.pMemPos, aRes.nMemLen, aRes.nSprmId);
            }
            else
                EndSprm( aRes.nSprmId ); // Switch off Attr
        }
        else if( aRes.nSprmId < 0x800 ) // Own helper attributes
        {
            if (bStartAttr)
            {
                nSkipChars = ImportExtSprm(&aRes);
                if (
                    (aRes.nSprmId == eFTN) || (aRes.nSprmId == eEDN) ||
                    (aRes.nSprmId == eFLD) || (aRes.nSprmId == eAND)
                   )
                {
                    WW8_CP nMaxLegalSkip = nTextEnd - rTextPos;
                    // Skip Field/Footnote-/End-Note here
                    rTextPos += std::min<WW8_CP>(nSkipChars, nMaxLegalSkip);
                    nSkipPos = rTextPos-1;
                }
            }
            else
                EndExtSprm( aRes.nSprmId );
        }
    }

    sal_Int32 nRequestedPos = m_xSBase->WW8Cp2Fc(m_xPlcxMan->GetCpOfs() + rTextPos, &m_bIsUnicode);
    bool bValidPos = checkSeek(*m_pStrm, nRequestedPos);
    SAL_WARN_IF(!bValidPos, "sw.ww8", "Document claimed to have text at an invalid position, skip attributes for region");

    // Find next Attr position (and Skip attributes of field contents if needed)
    if (nSkipChars && !m_bIgnoreText)
        m_xCtrlStck->MarkAllAttrsOld();
    bool bOldIgnoreText = m_bIgnoreText;
    m_bIgnoreText = true;
    sal_uInt16 nOldColl = m_nCurrentColl;
    bool bDoPlcxManPlusPLus = true;
    tools::Long nNext;
    do
    {
        if( bDoPlcxManPlusPLus )
            m_xPlcxMan->advance();
        nNext = bValidPos ? m_xPlcxMan->Where() : nTextEnd;

        if (m_pPostProcessAttrsInfo &&
            m_pPostProcessAttrsInfo->mnCpStart == nNext)
        {
            m_pPostProcessAttrsInfo->mbCopy = true;
        }

        if( (0 <= nNext) && (nSkipPos >= nNext) )
        {
            if (nDepthGuard >= 1024)
            {
                SAL_WARN("sw.ww8", "ReadTextAttr hit recursion limit");
                nNext = nTextEnd;
            }
            else
                nNext = ReadTextAttr(rTextPos, nTextEnd, rbStartLine, nDepthGuard + 1);
            bDoPlcxManPlusPLus = false;
            m_bIgnoreText = true;
        }

        if (m_pPostProcessAttrsInfo &&
            nNext > m_pPostProcessAttrsInfo->mnCpEnd)
        {
            m_pPostProcessAttrsInfo->mbCopy = false;
        }
    }
    while( nSkipPos >= nNext );
    m_bIgnoreText    = bOldIgnoreText;
    if( nSkipChars )
    {
        m_xCtrlStck->KillUnlockedAttrs( *m_pPaM->GetPoint() );
        if( nOldColl != m_xPlcxMan->GetColl() )
            ProcessCurrentCollChange(aRes, nullptr, false);
    }

    return nNext;
}

void SwWW8ImplReader::ReadAttrs(WW8_CP& rTextPos, WW8_CP& rNext, tools::Long nTextEnd, bool& rbStartLine)
{
    // Do we have attributes?
    if( rTextPos >= rNext )
    {
        do
        {
            rNext = ReadTextAttr(rTextPos, nTextEnd, rbStartLine);
            if (rTextPos == rNext && rTextPos >= nTextEnd)
                break;
        }
        while( rTextPos >= rNext );

    }
    else if ( rbStartLine )
    {
    /* No attributes, but still a new line.
     * If a line ends with a line break and paragraph attributes or paragraph templates
     * do NOT change the line end was not added to the Plcx.Fkp.papx i.e. (nFlags & MAN_MASK_NEW_PAP)
     * is false.
     * Due to this we need to set the template here as a kind of special treatment.
     */
        if (!m_bCpxStyle && m_nCurrentColl < m_vColl.size())
            SetTextFormatCollAndListLevel(*m_pPaM, m_vColl[m_nCurrentColl]);
        rbStartLine = false;
    }
}

/**
 * CloseAttrEnds to only read the attribute ends at the end of a text or a
 * text area (Header, Footnote, ...).
 * We ignore attribute starts and fields.
 */
void SwWW8ImplReader::CloseAttrEnds()
{
    // If there are any unclosed sprms then copy them to
    // another stack and close the ones that must be closed
    std::stack<sal_uInt16> aStack;
    m_xPlcxMan->TransferOpenSprms(aStack);

    while (!aStack.empty())
    {
        sal_uInt16 nSprmId = aStack.top();
        if ((0 < nSprmId) && (( eFTN > nSprmId) || (0x0800 <= nSprmId)))
            EndSprm(nSprmId);
        aStack.pop();
    }

    EndSpecial();
}

bool SwWW8ImplReader::ReadText(WW8_CP nStartCp, WW8_CP nTextLen, ManTypes nType)
{
    bool bJoined=false;

    bool bStartLine = true;
    short nCrCount = 0;
    short nDistance = 0;

    m_bWasParaEnd = false;
    m_nCurrentColl    =  0;
    m_xCurrentItemSet.reset();
    m_nCharFormat    = -1;
    m_bSpec = false;
    m_bPgSecBreak = false;

    m_xPlcxMan = std::make_shared<WW8PLCFMan>(m_xSBase.get(), nType, nStartCp);
    tools::Long nCpOfs = m_xPlcxMan->GetCpOfs(); // Offset for Header/Footer, Footnote

    WW8_CP nNext = m_xPlcxMan->Where();
    m_xPreviousNode.reset();
    sal_uInt8 nDropLines = 0;
    SwCharFormat* pNewSwCharFormat = nullptr;
    const SwCharFormat* pFormat = nullptr;

    bool bValidPos = checkSeek(*m_pStrm, m_xSBase->WW8Cp2Fc(nStartCp + nCpOfs, &m_bIsUnicode));
    if (!bValidPos)
        return false;

    WW8_CP l = nStartCp;
    const WW8_CP nMaxPossible = WW8_CP_MAX-nStartCp;
    if (nTextLen > nMaxPossible)
    {
        SAL_WARN_IF(nTextLen > nMaxPossible, "sw.ww8", "TextLen too long");
        nTextLen = nMaxPossible;
    }
    WW8_CP nTextEnd = nStartCp+nTextLen;
    while (l < nTextEnd)
    {
        ReadAttrs( l, nNext, nTextEnd, bStartLine );// Takes SectionBreaks into account, too
        OSL_ENSURE(m_pPaM->GetPointNode().GetTextNode(), "Missing txtnode");

        if (m_pPostProcessAttrsInfo != nullptr)
            PostProcessAttrs();

        if (l >= nTextEnd)
            break;

        bStartLine = ReadChars(l, nNext, nTextEnd, nCpOfs);

        // If the previous paragraph was a dropcap then do not
        // create a new txtnode and join the two paragraphs together
        if (bStartLine && !m_xPreviousNode) // Line end
        {
            bool bSplit = true;
            if (m_bCareFirstParaEndInToc)
            {
                m_bCareFirstParaEndInToc = false;
                if (m_pPaM->End() && m_pPaM->End()->GetNode().GetTextNode() &&  m_pPaM->End()->GetNode().GetTextNode()->Len() == 0)
                    bSplit = false;
            }
            if (m_bCareLastParaEndInToc)
            {
                m_bCareLastParaEndInToc = false;
                if (m_pPaM->End() && m_pPaM->End()->GetNode().GetTextNode() &&  m_pPaM->End()->GetNode().GetTextNode()->Len() == 0)
                    bSplit = false;
            }
            if (bSplit)
            {
                FinalizeTextNode(*m_pPaM->GetPoint());
            }
        }

        if (SwTextNode* pPreviousNode = (bStartLine && m_xPreviousNode) ? m_xPreviousNode->GetTextNode() : nullptr)
        {
            SwTextNode* pEndNd = m_pPaM->GetPointNode().GetTextNode();
            SAL_WARN_IF(!pEndNd, "sw.ww8", "didn't find textnode for dropcap");
            if (pEndNd)
            {
                const sal_Int32 nDropCapLen = pPreviousNode->GetText().getLength();

                // Need to reset the font size and text position for the dropcap
                {
                    SwPaM aTmp(*pEndNd, 0, *pEndNd, nDropCapLen+1);
                    m_xCtrlStck->Delete(aTmp);
                }

                // Get the default document dropcap which we can use as our template
                const SwFormatDrop* defaultDrop = GetFormatAttr(RES_PARATR_DROP);
                SwFormatDrop aDrop(*defaultDrop);

                aDrop.SetLines(nDropLines);
                aDrop.SetDistance(nDistance);
                aDrop.SetChars(writer_cast<sal_uInt8>(nDropCapLen));
                // Word has no concept of a "whole word dropcap"
                aDrop.SetWholeWord(false);

                if (pFormat)
                    aDrop.SetCharFormat(const_cast<SwCharFormat*>(pFormat));
                else if(pNewSwCharFormat)
                    aDrop.SetCharFormat(pNewSwCharFormat);

                SwPosition aStart(*pEndNd);
                m_xCtrlStck->NewAttr(aStart, aDrop);
                m_xCtrlStck->SetAttr(*m_pPaM->GetPoint(), RES_PARATR_DROP);
            }
            m_xPreviousNode.reset();
        }
        else if (m_bDropCap)
        {
            // If we have found a dropcap store the textnode
            m_xPreviousNode.reset(new TextNodeListener(m_pPaM->GetPointNode().GetTextNode()));

            SprmResult aDCS;
            if (m_bVer67)
                aDCS = m_xPlcxMan->GetPapPLCF()->HasSprm(46);
            else
                aDCS = m_xPlcxMan->GetPapPLCF()->HasSprm(0x442C);

            if (aDCS.pSprm && aDCS.nRemainingData >= 1)
                nDropLines = (*aDCS.pSprm) >> 3;
            else    // There is no Drop Cap Specifier hence no dropcap
                m_xPreviousNode.reset();

            SprmResult aDistance = m_xPlcxMan->GetPapPLCF()->HasSprm(0x842F);
            if (aDistance.pSprm && aDistance.nRemainingData >= 2)
                nDistance = SVBT16ToUInt16(aDistance.pSprm);
            else
                nDistance = 0;

            const SwFormatCharFormat *pSwFormatCharFormat = nullptr;

            if (m_xCurrentItemSet)
                pSwFormatCharFormat = &(m_xCurrentItemSet->Get(RES_TXTATR_CHARFMT));

            if (pSwFormatCharFormat)
                pFormat = pSwFormatCharFormat->GetCharFormat();

            if (m_xCurrentItemSet && !pFormat)
            {
                OUString sPrefix = "WW8Dropcap" + OUString::number(m_nDropCap++);
                pNewSwCharFormat = m_rDoc.MakeCharFormat(UIName(sPrefix), m_rDoc.GetDfltCharFormat());
                m_xCurrentItemSet->ClearItem(RES_CHRATR_ESCAPEMENT);
                pNewSwCharFormat->SetFormatAttr(*m_xCurrentItemSet);
            }

            m_xCurrentItemSet.reset();
            m_bDropCap=false;
        }

        if (bStartLine || m_bWasTabRowEnd)
        {
            // Call all 64 CRs; not for Header and the like
            if ((nCrCount++ & 0x40) == 0 && nType == MAN_MAINTEXT && l <= nTextLen)
            {
                if (nTextLen < WW8_CP_MAX/100)
                    m_nProgress = o3tl::narrowing<sal_uInt16>(l * 100 / nTextLen);
                else
                    m_nProgress = o3tl::narrowing<sal_uInt16>(l / nTextLen * 100);
                m_xProgress->Update(m_nProgress); // Update
            }
        }

        // If we have encountered a 0x0c which indicates either section of
        // pagebreak then look it up to see if it is a section break, and
        // if it is not then insert a page break. If it is a section break
        // it will be handled as such in the ReadAttrs of the next loop
        if (m_bPgSecBreak)
        {
            // We need only to see if a section is ending at this cp,
            // the plcf will already be sitting on the correct location
            // if it is there.
            WW8PLCFxDesc aTemp;
            aTemp.nStartPos = aTemp.nEndPos = WW8_CP_MAX;
            if (m_xPlcxMan->GetSepPLCF())
                m_xPlcxMan->GetSepPLCF()->GetSprms(&aTemp);
            if ((aTemp.nStartPos != l) && (aTemp.nEndPos != l))
            {
                // #i39251# - insert text node for page break, if no one inserted.
                // #i43118# - refine condition: the anchor
                // control stack has to have entries, otherwise it's not needed
                // to insert a text node.
                if (!bStartLine && !m_xAnchorStck->empty())
                {
                    FinalizeTextNode(*m_pPaM->GetPoint());
                }
                m_rDoc.getIDocumentContentOperations().InsertPoolItem(*m_pPaM,
                    SvxFormatBreakItem(SvxBreak::PageBefore, RES_BREAK));
                m_bFirstParaOfPage = true;
                m_bPgSecBreak = false;
            }
        }
    }

    m_xPreviousNode.reset();

    if (m_pPaM->GetPoint()->GetContentIndex())
        FinalizeTextNode(*m_pPaM->GetPoint());

    if (!m_bInHyperlink)
        bJoined = JoinNode(*m_pPaM);

    CloseAttrEnds();

    m_xPlcxMan.reset();
    return bJoined;
}

SwWW8ImplReader::SwWW8ImplReader(sal_uInt8 nVersionPara, SotStorage* pStorage,
    SvStream* pSt, SwDoc& rD, OUString aBaseURL, bool bNewDoc, bool bSkipImages, SwPosition const &rPos)
    : m_pDocShell(rD.GetDocShell())
    , m_pStg(pStorage)
    , m_pStrm(pSt)
    , m_pTableStream(nullptr)
    , m_pDataStream(nullptr)
    , m_rDoc(rD)
    , m_pPaM(nullptr)
    , m_aSectionManager(*this)
    , m_aExtraneousParas(rD)
    , m_aInsertedTables(rD)
    , m_aSectionNameGenerator(rD, u"WW"_ustr)
    , m_aGrfNameGenerator(bNewDoc, OUString('G'))
    , m_aParaStyleMapper(rD)
    , m_aCharStyleMapper(rD)
    , m_pFlyFormatOfJustInsertedGraphic(nullptr)
    , m_pPreviousNumPaM(nullptr)
    , m_pPrevNumRule(nullptr)
    , m_pCurrentColl(nullptr)
    , m_pDfltTextFormatColl(nullptr)
    , m_pStandardFormatColl(nullptr)
    , m_pDrawModel(nullptr)
    , m_pDrawPg(nullptr)
    , m_pNumFieldType(nullptr)
    , m_sBaseURL(std::move(aBaseURL))
    , m_nIniFlags(0)
    , m_nIniFlags1(0)
    , m_nFieldFlags(0)
    , m_bRegardHindiDigits( false )
    , m_bDrawCpOValid( false )
    , m_nDrawCpO(0)
    , m_nPicLocFc(0)
    , m_nObjLocFc(0)
    , m_nIniFlyDx(0)
    , m_nIniFlyDy(0)
    , m_eTextCharSet(RTL_TEXTENCODING_ASCII_US)
    , m_eStructCharSet(RTL_TEXTENCODING_ASCII_US)
    , m_eHardCharSet(RTL_TEXTENCODING_DONTKNOW)
    , m_nProgress(0)
    , m_nCurrentColl(0)
    , m_nFieldNum(0)
    , m_nLFOPosition(USHRT_MAX)
    , m_nCharFormat(0)
    , m_nDrawXOfs(0)
    , m_nDrawYOfs(0)
    , m_nDrawXOfs2(0)
    , m_nDrawYOfs2(0)
    , m_cSymbol(0)
    , m_nWantedVersion(nVersionPara)
    , m_nSwNumLevel(0xff)
    , m_nWwNumType(0xff)
    , m_pChosenWW8OutlineStyle(nullptr)
    , m_nListLevel(MAXLEVEL)
    , m_bNewDoc(bNewDoc)
    , m_bSkipImages(bSkipImages)
    , m_bReadNoTable(false)
    , m_bPgSecBreak(false)
    , m_bSpec(false)
    , m_bObj(false)
    , m_bTxbxFlySection(false)
    , m_bHasBorder(false)
    , m_bSymbol(false)
    , m_bIgnoreText(false)
    , m_nInTable(0)
    , m_bWasTabRowEnd(false)
    , m_bWasTabCellEnd(false)
    , m_bAnl(false)
    , m_bHdFtFootnoteEdn(false)
    , m_bFootnoteEdn(false)
    , m_bIsHeader(false)
    , m_bIsFooter(false)
    , m_bIsUnicode(false)
    , m_bCpxStyle(false)
    , m_bStyNormal(false)
    , m_bWWBugNormal(false)
    , m_bNoAttrImport(false)
    , m_bInHyperlink(false)
    , m_bWasParaEnd(false)
    , m_bVer67(false)
    , m_bVer6(false)
    , m_bVer7(false)
    , m_bVer8(false)
    , m_bEmbeddObj(false)
    , m_bCurrentAND_fNumberAcross(false)
    , m_bNoLnNumYet(true)
    , m_bFirstPara(true)
    , m_bFirstParaOfPage(false)
    , m_bParaAutoBefore(false)
    , m_bParaAutoAfter(false)
    , m_bDropCap(false)
    , m_nDropCap(0)
    , m_bBidi(false)
    , m_bReadTable(false)
    , m_bLoadingTOXCache(false)
    , m_nEmbeddedTOXLevel(0)
    , m_bLoadingTOXHyperlink(false)
    , m_bCareFirstParaEndInToc(false)
    , m_bCareLastParaEndInToc(false)
    , m_bNotifyMacroEventRead(false)
    , m_bFuzzing(comphelper::IsFuzzing())
{
    m_pStrm->SetEndian( SvStreamEndian::LITTLE );
    m_aApos.push_back(false);

    mpCursor = m_rDoc.CreateUnoCursor(rPos);
}

SwWW8ImplReader::~SwWW8ImplReader()
{
}

void SwWW8ImplReader::DeleteStack(std::unique_ptr<SwFltControlStack> pStck)
{
    if( pStck )
    {
        pStck->SetAttr( *m_pPaM->GetPoint(), 0, false);
        pStck->SetAttr( *m_pPaM->GetPoint(), 0, false);
    }
    else
    {
        OSL_ENSURE( false, "WW stack already deleted" );
    }
}

void wwSectionManager::SetSegmentToPageDesc(const wwSection &rSection,
    bool bIgnoreCols)
{
    SwPageDesc &rPage = *rSection.mpPage;

    SetNumberingType(rSection, rPage);

    SwFrameFormat &rFormat = rPage.GetMaster();

    if(mrReader.m_xWDop->fUseBackGroundInAllmodes) // #i56806# Make sure mrReader is initialized
        mrReader.GraphicCtor();

    if (mrReader.m_xWDop->fUseBackGroundInAllmodes && mrReader.m_xMSDffManager)
    {
        tools::Rectangle aRect(0, 0, 100, 100); // A dummy, we don't care about the size
        SvxMSDffImportData aData(aRect);
        rtl::Reference<SdrObject> pObject;
        if (mrReader.m_xMSDffManager->GetShape(0x401, pObject, aData) && !aData.empty())
        {
            // Only handle shape if it is a background shape
            if (aData.begin()->get()->nFlags & ShapeFlag::Background)
            {
                SfxItemSetFixed<RES_BACKGROUND, RES_BACKGROUND,XATTR_START, XATTR_END>
                    aSet(rFormat.GetDoc().GetAttrPool());
                mrReader.MatchSdrItemsIntoFlySet(pObject.get(), aSet, mso_lineSimple,
                                                 mso_lineSolid, mso_sptRectangle, aRect);
                if ( aSet.HasItem(RES_BACKGROUND) )
                    rFormat.SetFormatAttr(aSet.Get(RES_BACKGROUND));
                else
                    rFormat.SetFormatAttr(aSet);
            }
        }
    }
    wwULSpaceData aULData;
    GetPageULData(rSection, aULData);
    SetPageULSpaceItems(rFormat, aULData, rSection);

    rPage.SetVerticalAdjustment( rSection.mnVerticalAdjustment );

    SetPage(rPage, rFormat, rSection, bIgnoreCols);

    if (!(rSection.maSep.pgbApplyTo & 1))
        SwWW8ImplReader::SetPageBorder(rFormat, rSection);
    if (!(rSection.maSep.pgbApplyTo & 2))
        SwWW8ImplReader::SetPageBorder(rPage.GetFirstMaster(), rSection);

    mrReader.SetDocumentGrid(rFormat, rSection);
}

void wwSectionManager::SetUseOn(wwSection &rSection)
{
    bool bMirror = mrReader.m_xWDop->fMirrorMargins ||
        mrReader.m_xWDop->doptypography.m_f2on1;

    UseOnPage eUseBase = bMirror ? UseOnPage::Mirror : UseOnPage::All;
    UseOnPage eUse = eUseBase;
    if (!mrReader.m_xWDop->fFacingPages)
        eUse |= UseOnPage::HeaderShare | UseOnPage::FooterShare;
    if (!rSection.HasTitlePage())
        eUse |= UseOnPage::FirstShare;

    OSL_ENSURE(rSection.mpPage, "Makes no sense to call me with no pages to set");
    if (rSection.mpPage)
        rSection.mpPage->WriteUseOn(eUse);
}

/**
 * Set the page descriptor on this node, handle the different cases for a text
 * node or a table
 */
static void GiveNodePageDesc(SwNodeIndex const &rIdx, const SwFormatPageDesc &rPgDesc,
    SwDoc &rDoc)
{
    /*
    If it's a table here, apply the pagebreak to the table
    properties, otherwise we add it to the para at this
    position
    */
    if (rIdx.GetNode().IsTableNode())
    {
        SwTable& rTable =
            rIdx.GetNode().GetTableNode()->GetTable();
        SwFrameFormat* pApply = rTable.GetFrameFormat();
        OSL_ENSURE(pApply, "impossible");
        if (pApply)
            pApply->SetFormatAttr(rPgDesc);
    }
    else
    {
        SwPaM aPage(rIdx);
        rDoc.getIDocumentContentOperations().InsertPoolItem(aPage, rPgDesc);
    }
}

/**
 * Map a word section to a writer page descriptor
 */
SwFormatPageDesc wwSectionManager::SetSwFormatPageDesc(mySegIter const &rIter,
    mySegIter const &rStart, bool bIgnoreCols)
{
    if (mrReader.m_bNewDoc && rIter == rStart)
    {
        rIter->mpPage =
            mrReader.m_rDoc.getIDocumentStylePoolAccess().GetPageDescFromPool(RES_POOLPAGE_STANDARD);
    }
    else
    {
        rIter->mpPage = mrReader.m_rDoc.MakePageDesc(
            UIName(SwViewShell::GetShellRes()->GetPageDescName(mnDesc, ShellResource::NORMAL_PAGE)),
            nullptr, false);
    }
    OSL_ENSURE(rIter->mpPage, "no page!");
    if (!rIter->mpPage)
        return SwFormatPageDesc();

    // Set page before hd/ft
    const wwSection *pPrevious = nullptr;
    if (rIter != rStart)
        pPrevious = &(*(rIter-1));
    SetHdFt(*rIter, std::distance(rStart, rIter), pPrevious);
    SetUseOn(*rIter);

    // Set hd/ft after set page
    SetSegmentToPageDesc(*rIter, bIgnoreCols);

    SwFormatPageDesc aRet(rIter->mpPage);

    rIter->mpPage->SetFollow(rIter->mpPage);

    if (rIter->PageRestartNo())
        aRet.SetNumOffset(rIter->PageStartAt());

    ++mnDesc;
    return aRet;
}

void wwSectionManager::InsertSegments()
{
    mySegIter aEnd = maSegments.end();
    mySegIter aStart = maSegments.begin();
    for (mySegIter aIter = aStart; aIter != aEnd; ++aIter)
    {
        // If the section is of type "New column" (0x01), then simply insert a column break.
        // But only if there actually are columns on the page, otherwise a column break
        // seems to be handled like a page break by MSO.
        if ( aIter->maSep.bkc == 1 && aIter->maSep.ccolM1 > 0 )
        {
            SwPaM start( aIter->maStart );
            mrReader.m_rDoc.getIDocumentContentOperations().InsertPoolItem( start, SvxFormatBreakItem(SvxBreak::ColumnBefore, RES_BREAK));
            continue;
        }

        mySegIter aNext = aIter+1;
        mySegIter aPrev = (aIter == aStart) ? aIter : aIter-1;

        // If two following sections are different in following properties, Word will interpret a continuous
        // section break between them as if it was a section break next page.
        bool bThisAndPreviousAreCompatible = ((aIter->GetPageWidth() == aPrev->GetPageWidth()) &&
            (aIter->GetPageHeight() == aPrev->GetPageHeight()) && (aIter->IsLandScape() == aPrev->IsLandScape()));

        bool bInsertSection = (aIter != aStart) && aIter->IsContinuous() &&  bThisAndPreviousAreCompatible;
        bool bInsertPageDesc = !bInsertSection;
        bool bProtected = SectionIsProtected(*aIter); // do we really  need this ?? I guess I have a different logic in editshell which disables this...

        if (bInsertPageDesc)
        {
            /*
             If a cont section follows this section then we won't be
             creating a page desc with 2+ cols as we cannot host a one
             col section in a 2+ col pagedesc and make it look like
             word. But if the current section actually has columns then
             we are forced to insert a section here as well as a page
             descriptor.
            */

            bool bIgnoreCols = bInsertSection;
            bool bThisAndNextAreCompatible = (aNext == aEnd) ||
                ((aIter->GetPageWidth() == aNext->GetPageWidth()) &&
                 (aIter->GetPageHeight() == aNext->GetPageHeight()) &&
                 (aIter->IsLandScape() == aNext->IsLandScape()));

            if ((aNext != aEnd && aNext->IsContinuous() && bThisAndNextAreCompatible) || bProtected)
            {
                bIgnoreCols = true;
                if ((aIter->NoCols() > 1) || bProtected)
                    bInsertSection = true;
            }

            SwFormatPageDesc aDesc(SetSwFormatPageDesc(aIter, aStart, bIgnoreCols));
            if (!aDesc.GetPageDesc())
                continue;

            // special case handling for odd/even section break
            // a) as before create a new page style for the section break
            // b) set Layout of generated page style to right/left ( according
            //    to section break odd/even )
            // c) create a new style to follow the break page style
            if ( aIter->maSep.bkc == 3 || aIter->maSep.bkc == 4 )
            {
                // SetSwFormatPageDesc calls some methods that could
                // modify aIter (e.g. wwSection ).
                // Since  we call SetSwFormatPageDesc below to generate the
                // 'Following' style of the Break style, it is safer
                // to take  a copy of the contents of aIter.
                wwSection aTmpSection = *aIter;
                // create a new following page style
                SwFormatPageDesc aFollow(SetSwFormatPageDesc(aIter, aStart, bIgnoreCols));
                // restore any contents of aIter trashed by SetSwFormatPageDesc
                *aIter = std::move(aTmpSection);

                // Handle the section break
                UseOnPage eUseOnPage = UseOnPage::Left;
                if ( aIter->maSep.bkc == 4 ) // Odd ( right ) Section break
                    eUseOnPage = UseOnPage::Right;

                // Keep the share flags.
                aDesc.GetPageDesc()->SetUseOn( eUseOnPage );
                aDesc.GetPageDesc()->SetFollow( aFollow.GetPageDesc() );
            }

            // Avoid setting the page style at the very beginning since it is always the default style anyway,
            // unless it is needed to specify a page number.
            if (aIter != aStart || aDesc.GetNumOffset())
                GiveNodePageDesc(aIter->maStart, aDesc, mrReader.m_rDoc);
        }

        SwTextNode* pTextNd = nullptr;
        if (bInsertSection)
        {
            // Start getting the bounds of this section
            SwPaM aSectPaM(*mrReader.m_pPaM, mrReader.m_pPaM);
            SwNodeIndex aAnchor(aSectPaM.GetPoint()->GetNode());
            if (aNext != aEnd)
            {
                aAnchor = aNext->maStart;
                aSectPaM.GetPoint()->Assign(aAnchor);
                aSectPaM.Move(fnMoveBackward);
            }

            const SwPosition* pPos  = aSectPaM.GetPoint();
            SwTextNode const*const pSttNd = pPos->GetNode().GetTextNode();
            const SwTableNode* pTableNd = pSttNd ? pSttNd->FindTableNode() : nullptr;
            if (pTableNd)
            {
                pTextNd =
                    mrReader.m_rDoc.GetNodes().MakeTextNode(aAnchor.GetNode(),
                    mrReader.m_rDoc.getIDocumentStylePoolAccess().GetTextCollFromPool( RES_POOLCOLL_TEXT ));

                aSectPaM.GetPoint()->Assign(*pTextNd, 0);
            }

            aSectPaM.SetMark();

            aSectPaM.GetPoint()->Assign(aIter->maStart);

            bool bHasOwnHdFt = false;
            /*
             In this nightmare scenario the continuous section has its own
             headers and footers so we will try and find a hard page break
             between here and the end of the section and put the headers and
             footers there.
            */
            if (!bInsertPageDesc)
            {
               bHasOwnHdFt =
                mrReader.HasOwnHeaderFooter(
                 aIter->maSep.grpfIhdt & ~(WW8_HEADER_FIRST | WW8_FOOTER_FIRST),
                 aIter->maSep.grpfIhdt, std::distance(aStart, aIter)
                );
            }
            if (bHasOwnHdFt)
            {
                // #i40766# Need to cache the page descriptor in case there is
                // no page break in the section
                SwPageDesc *pOrig = aIter->mpPage;
                bool bFailed = true;
                SwFormatPageDesc aDesc(SetSwFormatPageDesc(aIter, aStart, true));
                if (aDesc.GetPageDesc())
                {
                    SwNodeOffset nStart = aSectPaM.Start()->GetNodeIndex();
                    SwNodeOffset nEnd   = aSectPaM.End()->GetNodeIndex();
                    for(; nStart <= nEnd; ++nStart)
                    {
                        SwNode* pNode = mrReader.m_rDoc.GetNodes()[nStart];
                        if (!pNode)
                            continue;
                        if (sw::util::HasPageBreak(*pNode))
                        {
                            SwNodeIndex aIdx(*pNode);
                            GiveNodePageDesc(aIdx, aDesc, mrReader.m_rDoc);
                            bFailed = false;
                            break;
                        }
                    }
                }
                if(bFailed)
                {
                    aIter->mpPage = pOrig;
                }
            }

            // End getting the bounds of this section, quite a job eh?
            SwSectionFormat *pRet = InsertSection(aSectPaM, *aIter);
            // The last section if continuous is always unbalanced
            if (pRet)
            {
                // Set the columns to be UnBalanced if that compatibility option is set
                if (mrReader.m_xWDop->fNoColumnBalance)
                    pRet->SetFormatAttr(SwFormatNoBalancedColumns(true));
                else
                {
                    // Otherwise set to unbalanced if the following section is
                    // not continuous, (which also means that the last section
                    // is unbalanced)
                    if (aNext == aEnd || !aNext->IsContinuous())
                        pRet->SetFormatAttr(SwFormatNoBalancedColumns(true));
                }
            }
        }

        if (pTextNd)
        {
            SwPaM aTest(*pTextNd);
            mrReader.m_rDoc.getIDocumentContentOperations().DelFullPara(aTest);
            pTextNd = nullptr;
        }
    }
}

void wwExtraneousParas::delete_all_from_doc()
{
    auto aEnd = m_aTextNodes.rend();
    for (auto aI = m_aTextNodes.rbegin(); aI != aEnd; ++aI)
    {
        ExtraTextNodeListener& rListener = const_cast<ExtraTextNodeListener&>(*aI);
        SwTextNode* pTextNode = rListener.GetTextNode();
        rListener.StopListening(pTextNode);

        SwPaM aTest(*pTextNode);
        m_rDoc.getIDocumentContentOperations().DelFullPara(aTest);
    }
    m_aTextNodes.clear();
}

void wwExtraneousParas::insert(SwTextNode *pTextNode)
{
    m_aTextNodes.emplace(pTextNode, this);
}

void wwExtraneousParas::remove_if_present(SwModify* pModify)
{
    auto it = std::find_if(m_aTextNodes.begin(), m_aTextNodes.end(),
        [pModify](const ExtraTextNodeListener& rEntry) { return rEntry.GetTextNode() == pModify; });
    if (it == m_aTextNodes.end())
        return;
    SAL_WARN("sw.ww8", "It is unexpected to drop a para scheduled for removal");
    m_aTextNodes.erase(it);
}

TextNodeListener::TextNodeListener(SwTextNode* pTextNode)
    : m_pTextNode(pTextNode)
{
    m_pTextNode->Add(*this);
}

TextNodeListener::~TextNodeListener()
{
    if (!m_pTextNode)
        return;
    StopListening(m_pTextNode);
}

void TextNodeListener::SwClientNotify(const SwModify& rModify, const SfxHint& rHint)
{
    if (rHint.GetId() != SfxHintId::SwObjectDying)
        return;
    // ofz#41398 drop a para scheduled for deletion if something else deletes it
    // before wwExtraneousParas gets its chance to do so. Not the usual scenario,
    // indicates an underlying bug.
    removed(const_cast<SwModify*>(&rModify));
}

void TextNodeListener::StopListening(SwModify* pTextNode)
{
    pTextNode->Remove(*this);
    m_pTextNode = nullptr;
}

void TextNodeListener::removed(SwModify* pTextNode)
{
    StopListening(pTextNode);
}

void wwExtraneousParas::ExtraTextNodeListener::removed(SwModify* pTextNode)
{
    m_pOwner->remove_if_present(pTextNode);
}

void SwWW8ImplReader::StoreMacroCmds()
{
    if (!m_xWwFib->m_lcbCmds)
        return;

    bool bValidPos = checkSeek(*m_pTableStream, m_xWwFib->m_fcCmds);
    if (!bValidPos)
        return;

    uno::Reference < embed::XStorage > xRoot(m_pDocShell->GetStorage());

    if (!xRoot.is())
        return;

    try
    {
        uno::Reference < io::XStream > xStream =
                xRoot->openStreamElement( SL::aMSMacroCmds, embed::ElementModes::READWRITE );
        std::unique_ptr<SvStream> xOutStream(::utl::UcbStreamHelper::CreateStream(xStream));

        sal_uInt32 lcbCmds = std::min<sal_uInt32>(m_xWwFib->m_lcbCmds, m_pTableStream->remainingSize());
        std::unique_ptr<sal_uInt8[]> xBuffer(new sal_uInt8[lcbCmds]);
        m_xWwFib->m_lcbCmds = m_pTableStream->ReadBytes(xBuffer.get(), lcbCmds);
        xOutStream->WriteBytes(xBuffer.get(), m_xWwFib->m_lcbCmds);
    }
    catch (...)
    {
    }
}

void SwWW8ImplReader::ReadDocVars()
{
    std::vector<OUString> aDocVarStrings;
    std::vector<ww::bytes> aDocVarStringIds;
    std::vector<OUString> aDocValueStrings;
    WW8ReadSTTBF(!m_bVer67, *m_pTableStream, m_xWwFib->m_fcStwUser,
        m_xWwFib->m_lcbStwUser, m_bVer67 ? 2 : 0, m_eStructCharSet,
        aDocVarStrings, &aDocVarStringIds, &aDocValueStrings);
    if (m_bVer67)        return;

    uno::Reference< text::XTextFieldsSupplier > xFieldsSupplier(m_pDocShell->GetModel(), uno::UNO_QUERY_THROW);
    uno::Reference<css::lang::XMultiServiceFactory> xTextFactory(m_pDocShell->GetModel(), uno::UNO_QUERY);
    uno::Reference< container::XNameAccess > xFieldMasterAccess = xFieldsSupplier->getTextFieldMasters();
    for(size_t i = 0; i < aDocVarStrings.size(); i++)
    {
        const OUString sName = sw::FilterControlChars(aDocVarStrings[i]);
        uno::Any aValue;
        if (aDocValueStrings.size() > i)
        {
            OUString value = aDocValueStrings[i];
            value = value.replaceAll("\r\n", "\n");
            value = value.replaceAll("\r", "\n");
            // fdo48097-1.doc is an example of a case needing sanitizeStringSurrogates
            aValue <<= comphelper::string::sanitizeStringSurrogates(value);
        }

        uno::Reference< beans::XPropertySet > xMaster;
        OUString sFieldMasterService("com.sun.star.text.FieldMaster.User." + sName);

        // Find or create Field Master
        if (xFieldMasterAccess->hasByName(sFieldMasterService))
        {
            xMaster.set(xFieldMasterAccess->getByName(sFieldMasterService), uno::UNO_QUERY_THROW);
        }
        else
        {
            xMaster.set(xTextFactory->createInstance(u"com.sun.star.text.FieldMaster.User"_ustr), uno::UNO_QUERY_THROW);
            xMaster->setPropertyValue(u"Name"_ustr, uno::Any(sName));
        }
        xMaster->setPropertyValue(u"Content"_ustr, aValue);
    }
}

/**
 * Document Info
 */
void SwWW8ImplReader::ReadDocInfo()
{
    if( !m_pStg )
        return;

    uno::Reference<document::XDocumentPropertiesSupplier> xDPS(
        m_pDocShell->GetModel(), uno::UNO_QUERY_THROW);
    uno::Reference<document::XDocumentProperties> xDocProps(
        xDPS->getDocumentProperties());
    OSL_ENSURE(xDocProps.is(), "DocumentProperties is null");

    if (!xDocProps.is())
        return;

    if ( m_xWwFib->m_fDot )
    {
        SfxMedium* pMedium = m_pDocShell->GetMedium();
        if ( pMedium )
        {
            const OUString& aName = pMedium->GetName();
            INetURLObject aURL( aName );
            OUString sTemplateURL = aURL.GetMainURL(INetURLObject::DecodeMechanism::ToIUri);
            if ( !sTemplateURL.isEmpty() )
                xDocProps->setTemplateURL( sTemplateURL );
        }
    }
    else if (m_xWwFib->m_lcbSttbfAssoc) // not a template, and has a SttbfAssoc
    {
        auto nCur = m_pTableStream->Tell();
        Sttb aSttb;
        // point at tgc record
        if (!checkSeek(*m_pTableStream, m_xWwFib->m_fcSttbfAssoc) || !aSttb.Read(*m_pTableStream))
            SAL_WARN("sw.ww8", "** Read of SttbAssoc data failed!!!! ");
        m_pTableStream->Seek( nCur ); // return to previous position, is that necessary?
        OUString sPath = aSttb.getStringAtIndex( 0x1 );
        OUString aURL;
        // attempt to convert to url (won't work for obvious reasons on linux)
        if ( !sPath.isEmpty() )
            osl::FileBase::getFileURLFromSystemPath( sPath, aURL );
        if (aURL.isEmpty())
            xDocProps->setTemplateURL( aURL );
        else
            xDocProps->setTemplateURL( sPath );

    }
    sfx2::LoadOlePropertySet(xDocProps, m_pStg);
}

static void lcl_createTemplateToProjectEntry( const uno::Reference< container::XNameContainer >& xPrjNameCache, const OUString& sTemplatePathOrURL, const OUString& sVBAProjName )
{
    if ( !xPrjNameCache.is() )
        return;

    INetURLObject aObj;
    aObj.SetURL( sTemplatePathOrURL );
    bool bIsURL = aObj.GetProtocol() != INetProtocol::NotValid;
    OUString aURL;
    if ( bIsURL )
        aURL = sTemplatePathOrURL;
    else
    {
        osl::FileBase::getFileURLFromSystemPath( sTemplatePathOrURL, aURL );
        aObj.SetURL( aURL );
    }
    try
    {
        OUString templateNameWithExt = aObj.GetLastName();
        sal_Int32 nIndex =  templateNameWithExt.lastIndexOf( '.' );
        if ( nIndex != -1 )
        {
            OUString templateName = templateNameWithExt.copy( 0, nIndex );
            xPrjNameCache->insertByName( templateName, uno::Any( sVBAProjName ) );
        }
    }
    catch( const uno::Exception& )
    {
    }
}

namespace {

class WW8Customizations
{
    SvStream* mpTableStream;
    WW8Fib mWw8Fib;
public:
    WW8Customizations( SvStream*, WW8Fib const & );
    void  Import( SwDocShell* pShell );
};

}

WW8Customizations::WW8Customizations( SvStream* pTableStream, WW8Fib const & rFib ) : mpTableStream(pTableStream), mWw8Fib( rFib )
{
}

void WW8Customizations::Import( SwDocShell* pShell )
{
    if ( mWw8Fib.m_lcbCmds == 0 || !IsEightPlus(mWw8Fib.GetFIBVersion()) )
        return;
    try
    {
        Tcg aTCG;
        sal_uInt64 nCur = mpTableStream->Tell();
        if (!checkSeek(*mpTableStream, mWw8Fib.m_fcCmds)) // point at tgc record
        {
            SAL_WARN("sw.ww8", "** Seek to Customization data failed!!!! ");
            return;
        }
        bool bReadResult = aTCG.Read( *mpTableStream );
        mpTableStream->Seek( nCur ); // return to previous position, is that necessary?
        if ( !bReadResult )
        {
            SAL_WARN("sw.ww8", "** Read of Customization data failed!!!! ");
            return;
        }
        aTCG.ImportCustomToolBar( *pShell );
    }
    catch(...)
    {
        SAL_WARN("sw.ww8", "** Read of Customization data failed!!!! epically");
    }
}

void SwWW8ImplReader::ReadGlobalTemplateSettings( std::u16string_view sCreatedFrom, const uno::Reference< container::XNameContainer >& xPrjNameCache )
{
    if (m_bFuzzing)
        return;

    SvtPathOptions aPathOpt;
    const OUString& aAddinPath = aPathOpt.GetAddinPath();
    uno::Sequence< OUString > sGlobalTemplates;

    // first get the autoload addins in the directory STARTUP
    uno::Reference<ucb::XSimpleFileAccess3> xSFA(ucb::SimpleFileAccess::create(::comphelper::getProcessComponentContext()));

    if( xSFA->isFolder( aAddinPath ) )
        sGlobalTemplates = xSFA->getFolderContents( aAddinPath, false );

    for (const auto& rGlobalTemplate : sGlobalTemplates)
    {
        INetURLObject aObj;
        aObj.SetURL( rGlobalTemplate );
        bool bIsURL = aObj.GetProtocol() != INetProtocol::NotValid;
        OUString aURL;
        if ( bIsURL )
                aURL = rGlobalTemplate;
        else
                osl::FileBase::getFileURLFromSystemPath( rGlobalTemplate, aURL );
        if ( !aURL.endsWithIgnoreAsciiCase( ".dot" ) || ( !sCreatedFrom.empty() && sCreatedFrom == aURL ) )
            continue; // don't try and read the same document as ourselves

        rtl::Reference<SotStorage> rRoot = new SotStorage(aURL, StreamMode::STD_READWRITE);

        BasicProjImportHelper aBasicImporter( *m_pDocShell );
        // Import vba via oox filter
        aBasicImporter.import( m_pDocShell->GetMedium()->GetInputStream() );
        lcl_createTemplateToProjectEntry( xPrjNameCache, aURL, aBasicImporter.getProjectName() );
        // Read toolbars & menus
        rtl::Reference<SotStorageStream> refMainStream = rRoot->OpenSotStream(u"WordDocument"_ustr);
        refMainStream->SetEndian(SvStreamEndian::LITTLE);
        WW8Fib aWwFib( *refMainStream, 8 );
        rtl::Reference<SotStorageStream> xTableStream =
                rRoot->OpenSotStream(aWwFib.m_fWhichTableStm ? SL::a1Table : SL::a0Table, StreamMode::STD_READ);

        if (xTableStream.is() && ERRCODE_NONE == xTableStream->GetError())
        {
            xTableStream->SetEndian(SvStreamEndian::LITTLE);
            WW8Customizations aGblCustomisations( xTableStream.get(), aWwFib );
            aGblCustomisations.Import( m_pDocShell );
        }
    }
}

ErrCode SwWW8ImplReader::CoreLoad(WW8Glossary const *pGloss)
{
    m_rDoc.SetDocumentType( SwDoc::DOCTYPE_MSWORD );
    if (m_bNewDoc && m_pStg && !pGloss)
    {
        // Initialize RDF metadata, to be able to add statements during the import.
        try
        {
            rtl::Reference<SwXTextDocument> const xModel(m_rDoc.GetDocShell()->GetBaseModel());
            const uno::Reference<uno::XComponentContext>& xComponentContext(comphelper::getProcessComponentContext());
            uno::Reference<embed::XStorage> xStorage = comphelper::OStorageHelper::GetTemporaryStorage();
            const uno::Reference<rdf::XURI> xBaseURI(sfx2::createBaseURI(xComponentContext, static_cast<SfxBaseModel*>(xModel.get()), m_sBaseURL));
            uno::Reference<task::XInteractionHandler> xHandler;
            xModel->loadMetadataFromStorage(xStorage, xBaseURI, xHandler);
        }
        catch (const uno::Exception&)
        {
            TOOLS_WARN_EXCEPTION("sw.ww8", "failed to initialize RDF metadata");
        }
        ReadDocInfo();
    }

    auto pFibData = std::make_shared<::ww8::WW8FibData>();

    if (m_xWwFib->m_fReadOnlyRecommended)
        pFibData->setReadOnlyRecommended(true);
    else
        pFibData->setReadOnlyRecommended(false);

    if (m_xWwFib->m_fWriteReservation)
        pFibData->setWriteReservation(true);
    else
        pFibData->setWriteReservation(false);

    m_rDoc.getIDocumentExternalData().setExternalData(::sw::tExternalDataType::FIB, pFibData);

    ::sw::tExternalDataPointer pSttbfAsoc
          = std::make_shared<::ww8::WW8Sttb<ww8::WW8Struct>>(*m_pTableStream, m_xWwFib->m_fcSttbfAssoc, m_xWwFib->m_lcbSttbfAssoc);

    m_rDoc.getIDocumentExternalData().setExternalData(::sw::tExternalDataType::STTBF_ASSOC, pSttbfAsoc);

    if (m_xWwFib->m_fWriteReservation || m_xWwFib->m_fReadOnlyRecommended)
    {
        SwDocShell * pDocShell = m_rDoc.GetDocShell();
        if (pDocShell)
            pDocShell->SetReadOnlyUI();
    }

    m_pPaM = mpCursor.get();

    m_xCtrlStck.reset(new SwWW8FltControlStack(m_rDoc, m_nFieldFlags, *this));

    m_xRedlineStack.reset(new sw::util::RedlineStack(m_rDoc));

    /*
        RefFieldStck: Keeps track of bookmarks which may be inserted as
        variables instead.
    */
    m_xReffedStck.reset(new SwWW8ReferencedFltEndStack(m_rDoc, m_nFieldFlags));
    m_xReffingStck.reset(new SwWW8FltRefStack(m_rDoc, m_nFieldFlags));

    m_xAnchorStck.reset(new SwWW8FltAnchorStack(m_rDoc, m_nFieldFlags));

    size_t nPageDescOffset = m_rDoc.GetPageDescCnt();

    RedlineFlags eMode = RedlineFlags::ShowInsert | RedlineFlags::ShowDelete;

    m_oSprmParser.emplace(*m_xWwFib);

    // Set handy helper variables
    m_bVer6  = (6 == m_xWwFib->m_nVersion);
    m_bVer7  = (7 == m_xWwFib->m_nVersion);
    m_bVer67 = m_bVer6 || m_bVer7;
    m_bVer8  = (8 == m_xWwFib->m_nVersion);

    m_eTextCharSet = WW8Fib::GetFIBCharset(m_xWwFib->m_chse, m_xWwFib->m_lid);
    m_eStructCharSet = WW8Fib::GetFIBCharset(m_xWwFib->m_chseTables, m_xWwFib->m_lid);

    m_bWWBugNormal = m_xWwFib->m_nProduct == 0xc03d;

    m_xProgress.reset(new ImportProgress(m_pDocShell, 0, 100));

    // read Font Table
    m_xFonts.reset(new WW8Fonts(*m_pTableStream, *m_xWwFib));

    // Document Properties
    m_xWDop.reset(new WW8Dop(*m_pTableStream, m_xWwFib->m_nFib, m_xWwFib->m_fcDop,
        m_xWwFib->m_lcbDop));

    if (m_bNewDoc)
        ImportDop();

    /*
        Import revisioning data: author names
    */
    if( m_xWwFib->m_lcbSttbfRMark )
    {
        ReadRevMarkAuthorStrTabl(*m_pTableStream,
                                 m_xWwFib->m_fcSttbfRMark,
                                 m_xWwFib->m_lcbSttbfRMark, m_rDoc);
    }

    // Initialize our String/ID map for Linked Sections
    std::vector<OUString> aLinkStrings;
    std::vector<ww::bytes> aStringIds;

    WW8ReadSTTBF(!m_bVer67, *m_pTableStream, m_xWwFib->m_fcSttbFnm,
        m_xWwFib->m_lcbSttbFnm, m_bVer67 ? 2 : 0, m_eStructCharSet,
        aLinkStrings, &aStringIds);

    for (size_t i=0; i < aLinkStrings.size() && i < aStringIds.size(); ++i)
    {
        const ww::bytes& stringId = aStringIds[i];
        if (stringId.size() < sizeof(WW8_STRINGID))
        {
            SAL_WARN("sw.ww8", "SwWW8ImplReader::CoreLoad: WW8_STRINGID is too short");
            continue;
        }
        const WW8_STRINGID *stringIdStruct = reinterpret_cast<const WW8_STRINGID*>(stringId.data());
        m_aLinkStringMap[SVBT16ToUInt16(stringIdStruct->nStringId)] = aLinkStrings[i];
    }

    ReadDocVars(); // import document variables as meta information.

    m_xProgress->Update(m_nProgress);    // Update

    m_xLstManager.reset(new WW8ListManager(*m_pTableStream, *this));

    /*
        first (1) import all styles (see WW8PAR2.CXX)
            BEFORE the import of the lists !!
    */
    m_xProgress->Update(m_nProgress);    // Update
    m_xStyles.reset(new WW8RStyle(*m_xWwFib, this)); // Styles
    m_xStyles->Import();

    /*
        In the end: (also see WW8PAR3.CXX)

        Go through all Styles and attach respective List Format
        AFTER we imported the Styles and AFTER we imported the Lists!
    */
    m_xProgress->Update(m_nProgress); // Update
    m_xStyles->PostProcessStyles();

    if (!m_vColl.empty())
        SetOutlineStyles();

    m_xSBase.reset(new WW8ScannerBase(m_pStrm,m_pTableStream,m_pDataStream, m_xWwFib.get()));

    if (m_xSBase->AreThereFootnotes())
    {
        static const SwFootnoteNum eNumA[4] =
        {
            FTNNUM_DOC, FTNNUM_CHAPTER, FTNNUM_PAGE, FTNNUM_DOC
        };

        SwFootnoteInfo aInfo = m_rDoc.GetFootnoteInfo(); // Copy-Ctor private

        aInfo.m_ePos = FTNPOS_PAGE;
        aInfo.m_eNum = eNumA[m_xWDop->rncFootnote];
        sal_uInt16 nfcFootnoteRef = m_xWDop->nfcFootnoteRef;
        aInfo.m_aFormat.SetNumberingType(WW8ListManager::GetSvxNumTypeFromMSONFC(nfcFootnoteRef));
        if( m_xWDop->nFootnote )
            aInfo.m_nFootnoteOffset = m_xWDop->nFootnote - 1;
        m_rDoc.SetFootnoteInfo( aInfo );
    }
    if (m_xSBase->AreThereEndnotes())
    {
        SwEndNoteInfo aInfo = m_rDoc.GetEndNoteInfo(); // Same as for Footnote
        sal_uInt16 nfcEdnRef = m_xWDop->nfcEdnRef;
        aInfo.m_aFormat.SetNumberingType(WW8ListManager::GetSvxNumTypeFromMSONFC(nfcEdnRef));
        if( m_xWDop->nEdn )
            aInfo.m_nFootnoteOffset = m_xWDop->nEdn - 1;
        m_rDoc.SetEndNoteInfo( aInfo );
    }

    if (m_xWwFib->m_lcbPlcfhdd)
        m_xHdFt.reset(new WW8PLCF_HdFt(m_pTableStream, *m_xWwFib, *m_xWDop));

    if (!m_bNewDoc)
    {
        // inserting into an existing document:
        // As only complete paragraphs are inserted, the current one
        // needs to be split - once or even twice.
        const SwPosition* pPos = m_pPaM->GetPoint();

        // split current paragraph to get new paragraph for the insertion
        m_rDoc.getIDocumentContentOperations().SplitNode( *pPos, false );

        // another split, if insertion position was not at the end of the current paragraph.
        SwTextNode const*const pTextNd = pPos->GetNode().GetTextNode();
        if ( pTextNd->GetText().getLength() )
        {
            m_rDoc.getIDocumentContentOperations().SplitNode( *pPos, false );
            // move PaM back to the newly empty paragraph
            m_pPaM->Move( fnMoveBackward );
        }

        // suppress insertion of tables inside footnotes.
        const SwNodeOffset nNd = pPos->GetNodeIndex();
        m_bReadNoTable = ( nNd < m_rDoc.GetNodes().GetEndOfInserts().GetIndex() &&
                       m_rDoc.GetNodes().GetEndOfInserts().StartOfSectionIndex() < nNd );
    }

    m_xProgress->Update(m_nProgress); // Update

    // loop for each glossary entry and add dummy section node
    if (pGloss)
    {
        WW8PLCF aPlc(*m_pTableStream, m_xWwFib->m_fcPlcfglsy, m_xWwFib->m_lcbPlcfglsy, 0);

        WW8_CP nStart, nEnd;
        void* pDummy;

        for (int i = 0; i < pGloss->GetNoStrings(); ++i, aPlc.advance())
        {
            SwNodeIndex aIdx( m_rDoc.GetNodes().GetEndOfContent());
            SwTextFormatColl* pColl =
                m_rDoc.getIDocumentStylePoolAccess().GetTextCollFromPool(RES_POOLCOLL_STANDARD,
                false);
            SwStartNode *pNode =
                m_rDoc.GetNodes().MakeTextSection(aIdx.GetNode(),
                SwNormalStartNode,pColl);
            m_pPaM->GetPoint()->Assign( pNode->GetIndex()+1 );
            aPlc.Get( nStart, nEnd, pDummy );
            ReadText(nStart,nEnd-nStart-1,MAN_MAINTEXT);
        }
    }
    else // ordinary case
    {
        if (m_bNewDoc && m_pStg) /*meaningless for a glossary */
        {
            m_pDocShell->SetIsTemplate( m_xWwFib->m_fDot ); // point at tgc record
            uno::Reference<document::XDocumentPropertiesSupplier> const
                xDocPropSupp(m_pDocShell->GetModel(), uno::UNO_QUERY_THROW);
            uno::Reference< document::XDocumentProperties > xDocProps( xDocPropSupp->getDocumentProperties(), uno::UNO_SET_THROW );

            OUString sCreatedFrom = xDocProps->getTemplateURL();
            uno::Reference< container::XNameContainer > xPrjNameCache;
            uno::Reference< lang::XMultiServiceFactory> xSF(m_pDocShell->GetModel(), uno::UNO_QUERY);
            if ( xSF.is() )
                xPrjNameCache.set( xSF->createInstance( u"ooo.vba.VBAProjectNameProvider"_ustr ), uno::UNO_QUERY );

            // Read Global templates
            ReadGlobalTemplateSettings( sCreatedFrom, xPrjNameCache );

            // Create and insert Word vba Globals
            uno::Any aGlobs;
            uno::Sequence< uno::Any > aArgs{ uno::Any(m_pDocShell->GetModel()) };
            try
            {
                aGlobs <<= ::comphelper::getProcessServiceFactory()->createInstanceWithArguments( u"ooo.vba.word.Globals"_ustr, aArgs );
            }
            catch (const uno::Exception&)
            {
                SAL_WARN("sw.ww8", "SwWW8ImplReader::CoreLoad: ooo.vba.word.Globals is not available");
            }

#if HAVE_FEATURE_SCRIPTING
            if (!m_bFuzzing)
            {
                BasicManager *pBasicMan = m_pDocShell->GetBasicManager();
                if (pBasicMan)
                    pBasicMan->SetGlobalUNOConstant( u"VBAGlobals"_ustr, aGlobs );
            }
#endif
            BasicProjImportHelper aBasicImporter( *m_pDocShell );
            // Import vba via oox filter
            bool bRet = aBasicImporter.import( m_pDocShell->GetMedium()->GetInputStream() );

            lcl_createTemplateToProjectEntry( xPrjNameCache, sCreatedFrom, aBasicImporter.getProjectName() );
            WW8Customizations aCustomisations( m_pTableStream, *m_xWwFib );
            aCustomisations.Import( m_pDocShell );

            if( bRet )
                m_rDoc.SetContainsMSVBasic(true);

            StoreMacroCmds();
        }
        ReadText(0, m_xWwFib->m_ccpText, MAN_MAINTEXT);
    }

    m_xProgress->Update(m_nProgress); // Update

    if (m_pDrawPg && m_xMSDffManager && m_xMSDffManager->GetShapeOrders())
    {
        // Helper array to chain the inserted frames (instead of SdrTextObj)
        SvxMSDffShapeTxBxSort aTxBxSort;

        // Ensure correct z-order of read Escher objects
        sal_uInt16 nShapeCount = m_xMSDffManager->GetShapeOrders()->size();

        for (sal_uInt16 nShapeNum=0; nShapeNum < nShapeCount; nShapeNum++)
        {
            SvxMSDffShapeOrder *pOrder =
                (*m_xMSDffManager->GetShapeOrders())[nShapeNum].get();
            // Insert Pointer into new Sort array
            if (pOrder->nTxBxComp && pOrder->pFly)
                aTxBxSort.insert(pOrder);
        }
        // Chain Frames
        if( !aTxBxSort.empty() )
        {
            SwFormatChain aChain;
            for( SvxMSDffShapeTxBxSort::iterator it = aTxBxSort.begin(); it != aTxBxSort.end(); ++it )
            {
                SvxMSDffShapeOrder *pOrder = *it;

                // Initialize FlyFrame Formats
                SwFlyFrameFormat* pFlyFormat     = pOrder->pFly;
                SwFlyFrameFormat* pNextFlyFormat = nullptr;
                SwFlyFrameFormat* pPrevFlyFormat = nullptr;

                // Determine successor, if we can
                SvxMSDffShapeTxBxSort::iterator tmpIter1 = it;
                ++tmpIter1;
                if( tmpIter1 != aTxBxSort.end() )
                {
                    SvxMSDffShapeOrder *pNextOrder = *tmpIter1;
                    if ((0xFFFF0000 & pOrder->nTxBxComp)
                           == (0xFFFF0000 & pNextOrder->nTxBxComp))
                        pNextFlyFormat = pNextOrder->pFly;
                }
                // Determine predecessor, if we can
                if( it != aTxBxSort.begin() )
                {
                    SvxMSDffShapeTxBxSort::iterator tmpIter2 = it;
                    --tmpIter2;
                    SvxMSDffShapeOrder *pPrevOrder = *tmpIter2;
                    if ((0xFFFF0000 & pOrder->nTxBxComp)
                           == (0xFFFF0000 & pPrevOrder->nTxBxComp))
                        pPrevFlyFormat = pPrevOrder->pFly;
                }
                // If successor or predecessor present, insert the
                // chain at the FlyFrame Format
                if (pNextFlyFormat || pPrevFlyFormat)
                {
                    aChain.SetNext( pNextFlyFormat );
                    aChain.SetPrev( pPrevFlyFormat );
                    pFlyFormat->SetFormatAttr( aChain );
                }
            }
        }
    }

    bool isHideRedlines(false);

    if (m_bNewDoc)
    {
        if( m_xWDop->fRevMarking )
            eMode |= RedlineFlags::On;
        isHideRedlines = !m_xWDop->fRMView;
    }

    m_aInsertedTables.DelAndMakeTableFrames();
    m_aSectionManager.InsertSegments();

    m_vColl.clear();

    m_xStyles.reset();

    m_xFormImpl.reset();
    GraphicDtor();
    m_xMSDffManager.reset();
    m_xHdFt.reset();
    m_xSBase.reset();
    m_xWDop.reset();
    m_xFonts.reset();
    m_xAtnNames.reset();
    m_oSprmParser.reset();
    m_xProgress.reset();

    m_pDataStream = nullptr;
    m_pTableStream = nullptr;

    DeleteCtrlStack();
    DeleteAnchorStack();
    DeleteRefStacks();
    m_oLastAnchorPos.reset();//ensure this is deleted before UpdatePageDescs
    // ofz#10994 remove any trailing fly paras before processing redlines
    m_xWFlyPara.reset();
    // ofz#12660 remove any trailing fly paras before deleting extra paras
    m_xSFlyPara.reset();
    // remove extra paragraphs after attribute ctrl
    // stacks etc. are destroyed, and before fields
    // are updated
    m_aExtraneousParas.delete_all_from_doc();
    m_xRedlineStack->closeall(*m_pPaM->GetPoint());

    // For i120928,achieve the graphics from the special bookmark with is for graphic bullet
    {
        std::vector<const SwGrfNode*> vecBulletGrf;
        std::vector<SwFrameFormat*> vecFrameFormat;

        IDocumentMarkAccess* const pMarkAccess = m_rDoc.getIDocumentMarkAccess();
        if ( pMarkAccess )
        {
            auto ppBkmk = pMarkAccess->findBookmark( SwMarkName(u"_PictureBullets"_ustr) );
            if ( ppBkmk != pMarkAccess->getBookmarksEnd() &&
                       IDocumentMarkAccess::GetType(**ppBkmk) == IDocumentMarkAccess::MarkType::BOOKMARK )
            {
                SwTextNode* pTextNode = (*ppBkmk)->GetMarkStart().GetNode().GetTextNode();

                if ( pTextNode )
                {
                    const SwpHints* pHints = pTextNode->GetpSwpHints();
                    for( size_t nHintPos = 0; pHints && nHintPos < pHints->Count(); ++nHintPos)
                    {
                        const SwTextAttr *pHt = pHints->Get(nHintPos);
                        if (pHt->Which() != RES_TXTATR_FLYCNT)
                            continue;
                        const sal_Int32 st = pHt->GetStart();
                        if (st >= (*ppBkmk)->GetMarkStart().GetContentIndex())
                        {
                            SwFrameFormat* pFrameFormat = pHt->GetFlyCnt().GetFrameFormat();
                            vecFrameFormat.push_back(pFrameFormat);
                            const SwNodeIndex* pNdIdx = pFrameFormat->GetContent().GetContentIdx();
                            const SwNodes* pNodesArray = (pNdIdx != nullptr)
                                                         ? &(pNdIdx->GetNodes())
                                                         : nullptr;
                            const SwGrfNode *pGrf = (pNodesArray != nullptr)
                                                    ? (*pNodesArray)[pNdIdx->GetIndex() + 1]->GetGrfNode()
                                                    : nullptr;
                            vecBulletGrf.push_back(pGrf);
                        }
                    }
                    // update graphic bullet information
                    size_t nCount = m_xLstManager->GetWW8LSTInfoNum();
                    for (size_t i = 0; i < nCount; ++i)
                    {
                        SwNumRule* pRule = m_xLstManager->GetNumRule(i);
                        for (sal_uInt16 j = 0; j < MAXLEVEL; ++j)
                        {
                            SwNumFormat aNumFormat(pRule->Get(j));
                            const sal_Int16 nType = aNumFormat.GetNumberingType();
                            const sal_uInt16 nGrfBulletCP = aNumFormat.GetGrfBulletCP();
                            if ( nType == SVX_NUM_BITMAP
                                 && vecBulletGrf.size() > nGrfBulletCP
                                 && vecBulletGrf[nGrfBulletCP] != nullptr )
                            {
                                Graphic aGraphic = vecBulletGrf[nGrfBulletCP]->GetGrf();
                                SvxBrushItem aBrush(aGraphic, GPOS_AREA, SID_ATTR_BRUSH);
                                const vcl::Font& aFont = numfunc::GetDefBulletFont();
                                int nHeight = aFont.GetFontHeight() * 12;
                                Size aPrefSize( aGraphic.GetPrefSize());
                                if (aPrefSize.Height() * aPrefSize.Width() != 0 )
                                {
                                    int nWidth = (nHeight * aPrefSize.Width()) / aPrefSize.Height();
                                    Size aSize(nWidth, nHeight);
                                    aNumFormat.SetGraphicBrush(&aBrush, &aSize);
                                }
                                else
                                {
                                    aNumFormat.SetNumberingType(SVX_NUM_CHAR_SPECIAL);
                                    aNumFormat.SetBulletChar(0x2190);
                                }
                                pRule->Set( j, aNumFormat );
                            }
                        }
                    }
                    // Remove additional pictures
                    for (SwFrameFormat* p : vecFrameFormat)
                    {
                        m_rDoc.getIDocumentLayoutAccess().DelLayoutFormat(p);
                    }
                }
            }
        }
        m_xLstManager.reset();
    }

    m_oPosAfterTOC.reset();
    m_xRedlineStack.reset();
    mpCursor.reset();
    m_pPaM = nullptr;

    UpdateFields();

    // delete the pam before the call for hide all redlines (Bug 73683)
    if (m_bNewDoc)
      m_rDoc.getIDocumentRedlineAccess().SetRedlineFlags(eMode);

    UpdatePageDescs(m_rDoc, nPageDescOffset);

    // can't set it on the layout or view shell because it doesn't exist yet
    m_rDoc.GetDocumentRedlineManager().SetHideRedlines(isHideRedlines);

    return ERRCODE_NONE;
}

ErrCode SwWW8ImplReader::SetSubStreams(rtl::Reference<SotStorageStream>& rTableStream,
                                       rtl::Reference<SotStorageStream>& rDataStream)
{
    ErrCode nErrRet = ERRCODE_NONE;
    // 6 stands for "6 OR 7", 7 stands for "ONLY 7"
    switch (m_xWwFib->m_nVersion)
    {
        case 6:
        case 7:
            m_pTableStream = m_pStrm;
            m_pDataStream = m_pStrm;
            break;
        case 8:
            if(!m_pStg)
            {
                OSL_ENSURE( m_pStg, "Version 8 always needs to have a Storage!!" );
                nErrRet = ERR_SWG_READ_ERROR;
                break;
            }

            rTableStream = m_pStg->OpenSotStream(
                m_xWwFib->m_fWhichTableStm ? SL::a1Table : SL::a0Table,
                StreamMode::STD_READ);

            m_pTableStream = rTableStream.get();
            m_pTableStream->SetEndian( SvStreamEndian::LITTLE );

            rDataStream = m_pStg->OpenSotStream(SL::aData, StreamMode::STD_READ);

            if (rDataStream.is() && ERRCODE_NONE == rDataStream->GetError())
            {
                m_pDataStream = rDataStream.get();
                m_pDataStream->SetEndian(SvStreamEndian::LITTLE);
            }
            else
                m_pDataStream = m_pStrm;
            break;
        default:
            // Program error!
            OSL_ENSURE( false, "We forgot to encode nVersion!" );
            nErrRet = ERR_SWG_READ_ERROR;
            break;
    }
    return nErrRet;
}

namespace
{
    SvStream* MakeTemp(std::optional<utl::TempFileFast>& roTempFile)
    {
        roTempFile.emplace();
        return roTempFile->GetStream(StreamMode::READWRITE | StreamMode::SHARE_DENYWRITE);
    }

#define WW_BLOCKSIZE 0x200

    void DecryptRC4(msfilter::MSCodec97& rCtx, SvStream &rIn, SvStream &rOut)
    {
        const std::size_t nLen = rIn.TellEnd();
        rIn.Seek(0);

        sal_uInt8 in[WW_BLOCKSIZE];
        for (std::size_t nI = 0, nBlock = 0; nI < nLen; nI += WW_BLOCKSIZE, ++nBlock)
        {
            std::size_t nBS = std::min<size_t>(nLen - nI, WW_BLOCKSIZE);
            nBS = rIn.ReadBytes(in, nBS);
            rCtx.InitCipher(nBlock);
            rCtx.Decode(in, nBS, in, nBS);
            rOut.WriteBytes(in, nBS);
        }
    }

    void DecryptXOR(msfilter::MSCodec_XorWord95 &rCtx, SvStream &rIn, SvStream &rOut)
    {
        std::size_t nSt = rIn.Tell();
        std::size_t nLen = rIn.TellEnd();

        rCtx.InitCipher();
        rCtx.Skip(nSt);

        sal_uInt8 in[0x4096];
        for (std::size_t nI = nSt; nI < nLen; nI += 0x4096)
        {
            std::size_t nBS = std::min<size_t>(nLen - nI, 0x4096 );
            nBS = rIn.ReadBytes(in, nBS);
            rCtx.Decode(in, nBS);
            rOut.WriteBytes(in, nBS);
        }
    }

    // moan, copy and paste :-(
    OUString QueryPasswordForMedium(SfxMedium& rMedium)
    {
        OUString aPassw;

        if (const SfxStringItem* pPasswordItem = rMedium.GetItemSet().GetItemIfSet(SID_PASSWORD))
            aPassw = pPasswordItem->GetValue();
        else
        {
            try
            {
                uno::Reference< task::XInteractionHandler > xHandler( rMedium.GetInteractionHandler() );
                if( xHandler.is() )
                {
                    rtl::Reference<::comphelper::DocPasswordRequest> pRequest = new ::comphelper::DocPasswordRequest(
                        ::comphelper::DocPasswordRequestType::MS, task::PasswordRequestMode_PASSWORD_ENTER,
                        INetURLObject(rMedium.GetOrigURL())
                            .GetLastName(INetURLObject::DecodeMechanism::WithCharset));

                    xHandler->handle( pRequest );

                    if( pRequest->isPassword() )
                        aPassw = pRequest->getPassword();
                }
            }
            catch( const uno::Exception& )
            {
            }
        }

        return aPassw;
    }

    uno::Sequence< beans::NamedValue > InitXorWord95Codec( ::msfilter::MSCodec_XorWord95& rCodec, SfxMedium& rMedium, WW8Fib const * pWwFib )
    {
        uno::Sequence< beans::NamedValue > aEncryptionData;
        const SfxUnoAnyItem* pEncryptionData = rMedium.GetItemSet().GetItem(SID_ENCRYPTIONDATA, false);
        if ( pEncryptionData && ( pEncryptionData->GetValue() >>= aEncryptionData ) && !rCodec.InitCodec( aEncryptionData ) )
            aEncryptionData.realloc( 0 );

        if ( !aEncryptionData.hasElements() )
        {
            OUString sUniPassword = QueryPasswordForMedium( rMedium );

            OString sPassword(OUStringToOString(sUniPassword,
                WW8Fib::GetFIBCharset(pWwFib->m_chseTables, pWwFib->m_lid)));

            sal_Int32 nLen = sPassword.getLength();
            if( nLen <= 15 )
            {
                sal_uInt8 pPassword[16];
                memcpy(pPassword, sPassword.getStr(), nLen);
                memset(pPassword+nLen, 0, sizeof(pPassword)-nLen);

                rCodec.InitKey( pPassword );
                aEncryptionData = rCodec.GetEncryptionData();

                // the export supports RC4 algorithm only, so we have to
                // generate the related EncryptionData as well, so that Save
                // can export the document without asking for a password;
                // as result there will be EncryptionData for both algorithms
                // in the MediaDescriptor
                ::msfilter::MSCodec_Std97 aCodec97;

                sal_uInt8 pDocId[ 16 ];
                if (rtl_random_getBytes(nullptr, pDocId, 16) != rtl_Random_E_None)
                {
                    throw uno::RuntimeException(u"rtl_random_getBytes failed"_ustr);
                }

                sal_uInt16 pStd97Pass[16] = {};
                for( sal_Int32 nChar = 0; nChar < nLen; ++nChar )
                    pStd97Pass[nChar] = sUniPassword[nChar];

                aCodec97.InitKey( pStd97Pass, pDocId );

                // merge the EncryptionData, there should be no conflicts
                ::comphelper::SequenceAsHashMap aEncryptionHash( aEncryptionData );
                aEncryptionHash.update( ::comphelper::SequenceAsHashMap( aCodec97.GetEncryptionData() ) );
                aEncryptionHash >> aEncryptionData;
            }
        }

        return aEncryptionData;
    }

    uno::Sequence< beans::NamedValue > Init97Codec(msfilter::MSCodec97& rCodec, sal_uInt8 const pDocId[16], SfxMedium& rMedium)
    {
        uno::Sequence< beans::NamedValue > aEncryptionData;
        const SfxUnoAnyItem* pEncryptionData = rMedium.GetItemSet().GetItem(SID_ENCRYPTIONDATA, false);
        if ( pEncryptionData && ( pEncryptionData->GetValue() >>= aEncryptionData ) && !rCodec.InitCodec( aEncryptionData ) )
            aEncryptionData.realloc( 0 );

        if ( !aEncryptionData.hasElements() )
        {
            OUString sUniPassword = QueryPasswordForMedium( rMedium );

            sal_Int32 nLen = sUniPassword.getLength();
            if ( nLen <= 15 )
            {
                sal_uInt16 pPassword[16] = {};
                for( sal_Int32 nChar = 0; nChar < nLen; ++nChar )
                    pPassword[nChar] = sUniPassword[nChar];

                rCodec.InitKey( pPassword, pDocId );
                aEncryptionData = rCodec.GetEncryptionData();
            }
        }

        return aEncryptionData;
    }
}

//TO-DO: merge this with lclReadFilepass8_Strong in sc which uses a different
//stream thing
static bool lclReadCryptoAPIHeader(msfilter::RC4EncryptionInfo &info, SvStream &rStream)
{
    //It is possible there are other variants in existence but these
    //are the defaults I get with Word 2013

    rStream.ReadUInt32(info.header.flags);
    if (oox::getFlag( info.header.flags, msfilter::ENCRYPTINFO_EXTERNAL))
        return false;

    sal_uInt32 nHeaderSize(0);
    rStream.ReadUInt32(nHeaderSize);
    sal_uInt32 actualHeaderSize = sizeof(info.header);

    if (nHeaderSize < actualHeaderSize)
        return false;

    rStream.ReadUInt32(info.header.flags);
    rStream.ReadUInt32(info.header.sizeExtra);
    rStream.ReadUInt32(info.header.algId);
    rStream.ReadUInt32(info.header.algIdHash);
    rStream.ReadUInt32(info.header.keyBits);
    rStream.ReadUInt32(info.header.providedType);
    rStream.ReadUInt32(info.header.reserved1);
    rStream.ReadUInt32(info.header.reserved2);

    rStream.SeekRel(nHeaderSize - actualHeaderSize);

    rStream.ReadUInt32(info.verifier.saltSize);
    if (info.verifier.saltSize != msfilter::SALT_LENGTH)
        return false;
    rStream.ReadBytes(&info.verifier.salt, sizeof(info.verifier.salt));
    rStream.ReadBytes(&info.verifier.encryptedVerifier, sizeof(info.verifier.encryptedVerifier));

    rStream.ReadUInt32(info.verifier.encryptedVerifierHashSize);
    if (info.verifier.encryptedVerifierHashSize != RTL_DIGEST_LENGTH_SHA1)
        return false;
    rStream.ReadBytes(&info.verifier.encryptedVerifierHash, info.verifier.encryptedVerifierHashSize);

    // check flags and algorithm IDs, required are AES128 and SHA-1
    if (!oox::getFlag(info.header.flags, msfilter::ENCRYPTINFO_CRYPTOAPI))
        return false;

    if (oox::getFlag(info.header.flags, msfilter::ENCRYPTINFO_AES))
        return false;

    if (info.header.algId != msfilter::ENCRYPT_ALGO_RC4)
        return false;

    // hash algorithm ID 0 defaults to SHA-1 too
    if (info.header.algIdHash != 0 && info.header.algIdHash != msfilter::ENCRYPT_HASH_SHA1)
        return false;

    return true;
}

ErrCode SwWW8ImplReader::LoadThroughDecryption(WW8Glossary *pGloss)
{
    ErrCode nErrRet = ERRCODE_NONE;
    if (pGloss)
        m_xWwFib = pGloss->GetFib();
    else
        m_xWwFib = std::make_shared<WW8Fib>(*m_pStrm, m_nWantedVersion);

    if (m_xWwFib->m_nFibError)
        nErrRet = ERR_SWG_READ_ERROR;

    rtl::Reference<SotStorageStream> xTableStream, xDataStream;

    if (!nErrRet)
        nErrRet = SetSubStreams(xTableStream, xDataStream);

    std::optional<utl::TempFileFast> oTempMain;
    std::optional<utl::TempFileFast> oTempTable;
    std::optional<utl::TempFileFast> oTempData;
    SvStream* pDecryptMain = nullptr;
    SvStream* pDecryptTable = nullptr;
    SvStream* pDecryptData = nullptr;

    bool bDecrypt = false;
    enum {RC4CryptoAPI, RC4, XOR, Other} eAlgo = Other;
    if (m_xWwFib->m_fEncrypted && !nErrRet)
    {
        if (!pGloss)
        {
            bDecrypt = true;
            if (8 != m_xWwFib->m_nVersion)
                eAlgo = XOR;
            else
            {
                if (m_xWwFib->m_nKey != 0)
                    eAlgo = XOR;
                else
                {
                    m_pTableStream->Seek(0);
                    sal_uInt32 nEncType(0);
                    m_pTableStream->ReadUInt32(nEncType);
                    if (nEncType == msfilter::VERSION_INFO_1997_FORMAT)
                        eAlgo = RC4;
                    else if (nEncType == msfilter::VERSION_INFO_2007_FORMAT || nEncType == msfilter::VERSION_INFO_2007_FORMAT_SP2)
                        eAlgo = RC4CryptoAPI;
                }
            }
        }
    }

    if (bDecrypt)
    {
        nErrRet = ERRCODE_SVX_WRONGPASS;
        SfxMedium* pMedium = m_pDocShell->GetMedium();

        if ( pMedium )
        {
            switch (eAlgo)
            {
                default:
                    nErrRet = ERRCODE_SVX_READ_FILTER_CRYPT;
                    break;
                case XOR:
                {
                    msfilter::MSCodec_XorWord95 aCtx;
                    uno::Sequence< beans::NamedValue > aEncryptionData = InitXorWord95Codec(aCtx, *pMedium, m_xWwFib.get());

                    // if initialization has failed the EncryptionData should be empty
                    if (aEncryptionData.hasElements() && aCtx.VerifyKey(m_xWwFib->m_nKey, m_xWwFib->m_nHash))
                    {
                        nErrRet = ERRCODE_NONE;
                        pDecryptMain = MakeTemp(oTempMain);

                        m_pStrm->Seek(0);
                        size_t nUnencryptedHdr =
                            (8 == m_xWwFib->m_nVersion) ? 0x44 : 0x34;
                        std::unique_ptr<sal_uInt8[]> pIn(new sal_uInt8[nUnencryptedHdr]);
                        nUnencryptedHdr = m_pStrm->ReadBytes(pIn.get(), nUnencryptedHdr);
                        pDecryptMain->WriteBytes(pIn.get(), nUnencryptedHdr);
                        pIn.reset();

                        DecryptXOR(aCtx, *m_pStrm, *pDecryptMain);

                        if (!m_pTableStream || m_pTableStream == m_pStrm)
                            m_pTableStream = pDecryptMain;
                        else
                        {
                            pDecryptTable = MakeTemp(oTempTable);
                            DecryptXOR(aCtx, *m_pTableStream, *pDecryptTable);
                            m_pTableStream = pDecryptTable;
                        }

                        if (!m_pDataStream || m_pDataStream == m_pStrm)
                            m_pDataStream = pDecryptMain;
                        else
                        {
                            pDecryptData = MakeTemp(oTempData);
                            DecryptXOR(aCtx, *m_pDataStream, *pDecryptData);
                            m_pDataStream = pDecryptData;
                        }

                        pMedium->GetItemSet().ClearItem( SID_PASSWORD );
                        pMedium->GetItemSet().Put( SfxUnoAnyItem( SID_ENCRYPTIONDATA, uno::Any( aEncryptionData ) ) );
                    }
                }
                break;
                case RC4:
                case RC4CryptoAPI:
                {
                    std::unique_ptr<msfilter::MSCodec97> xCtx;
                    msfilter::RC4EncryptionInfo info;
                    bool bCouldReadHeaders;

                    if (eAlgo == RC4)
                    {
                        xCtx.reset(new msfilter::MSCodec_Std97);
                        assert(sizeof(info.verifier.encryptedVerifierHash) >= RTL_DIGEST_LENGTH_MD5);
                        bCouldReadHeaders =
                            checkRead(*m_pTableStream, info.verifier.salt, sizeof(info.verifier.salt)) &&
                            checkRead(*m_pTableStream, info.verifier.encryptedVerifier, sizeof(info.verifier.encryptedVerifier)) &&
                            checkRead(*m_pTableStream, info.verifier.encryptedVerifierHash, RTL_DIGEST_LENGTH_MD5);
                    }
                    else
                    {
                        xCtx.reset(new msfilter::MSCodec_CryptoAPI);
                        bCouldReadHeaders = lclReadCryptoAPIHeader(info, *m_pTableStream);
                    }

                    // if initialization has failed the EncryptionData should be empty
                    uno::Sequence< beans::NamedValue > aEncryptionData;
                    if (bCouldReadHeaders)
                        aEncryptionData = Init97Codec(*xCtx, info.verifier.salt, *pMedium);
                    else
                        nErrRet = ERRCODE_SVX_READ_FILTER_CRYPT;
                    if (aEncryptionData.hasElements() && xCtx->VerifyKey(info.verifier.encryptedVerifier,
                                                                       info.verifier.encryptedVerifierHash))
                    {
                        nErrRet = ERRCODE_NONE;

                        pDecryptMain = MakeTemp(oTempMain);

                        m_pStrm->Seek(0);
                        std::size_t nUnencryptedHdr = 0x44;
                        std::unique_ptr<sal_uInt8[]> pIn(new sal_uInt8[nUnencryptedHdr]);
                        nUnencryptedHdr = m_pStrm->ReadBytes(pIn.get(), nUnencryptedHdr);

                        DecryptRC4(*xCtx, *m_pStrm, *pDecryptMain);

                        pDecryptMain->Seek(0);
                        pDecryptMain->WriteBytes(pIn.get(), nUnencryptedHdr);
                        pIn.reset();

                        pDecryptTable = MakeTemp(oTempTable);
                        DecryptRC4(*xCtx, *m_pTableStream, *pDecryptTable);
                        m_pTableStream = pDecryptTable;

                        if (!m_pDataStream || m_pDataStream == m_pStrm)
                            m_pDataStream = pDecryptMain;
                        else
                        {
                            pDecryptData = MakeTemp(oTempData);
                            DecryptRC4(*xCtx, *m_pDataStream, *pDecryptData);
                            m_pDataStream = pDecryptData;
                        }

                        pMedium->GetItemSet().ClearItem( SID_PASSWORD );
                        pMedium->GetItemSet().Put( SfxUnoAnyItem( SID_ENCRYPTIONDATA, uno::Any( aEncryptionData ) ) );
                    }
                }
                break;
            }
        }

        if (nErrRet == ERRCODE_NONE)
        {
            m_pStrm = pDecryptMain;
            assert(m_pStrm);
            m_xWwFib = std::make_shared<WW8Fib>(*m_pStrm, m_nWantedVersion);
            if (m_xWwFib->m_nFibError)
                nErrRet = ERR_SWG_READ_ERROR;
        }
    }

    if (!nErrRet)
        nErrRet = CoreLoad(pGloss);

    oTempMain.reset();
    oTempTable.reset();
    oTempData.reset();

    m_xWwFib.reset();
    return nErrRet;
}

void SwWW8ImplReader::SetOutlineStyles()
{
    // If we are inserted into a document then don't clobber existing outline
    // levels.
    sal_uInt16 nOutlineStyleListLevelWithAssignment = 0;
    if (!m_bNewDoc)
    {
        ww8::ParaStyles aOutLined(sw::util::GetParaStyles(m_rDoc));
        sw::util::SortByAssignedOutlineStyleListLevel(aOutLined);
        ww8::ParaStyles::reverse_iterator aEnd = aOutLined.rend();
        for ( ww8::ParaStyles::reverse_iterator aIter = aOutLined.rbegin(); aIter < aEnd; ++aIter)
        {
            if ((*aIter)->IsAssignedToListLevelOfOutlineStyle())
                nOutlineStyleListLevelWithAssignment |= 1 << (*aIter)->GetAssignedOutlineStyleLevel();
            else
                break;
        }
    }

    // Check applied WW8 list styles at WW8 Built-In Heading Styles
    // - Choose the list style which occurs most often as the one which provides
    //   the list level properties for the Outline Style.
    // - Populate temporary list of WW8 Built-In Heading Styles for further
    // iteration
    std::vector<SwWW8StyInf*> aWW8BuiltInHeadingStyles;
    {
        sal_uInt16 nStyle = 0;
        std::map<const SwNumRule*, int> aWW8ListStyleCounts;
        std::map<const SwNumRule*, bool> aPreventUseAsChapterNumbering;
        for (SwWW8StyInf& rSI : m_vColl)
        {
            // Copy inherited numbering info since LO drops inheritance after ChapterNumbering
            // and only applies listLevel via style with the selected ChapterNumbering LFO.
            bool bReRegister = false;
            if (rSI.m_nBase && rSI.m_nBase < m_vColl.size()
                && m_vColl[rSI.m_nBase].HasWW8OutlineLevel())
            {
                if (rSI.m_nLFOIndex == USHRT_MAX)
                {
                    rSI.m_nLFOIndex = m_vColl[rSI.m_nBase].m_nLFOIndex;

                    // When ANYTHING is wrong or strange, prohibit eligibility for ChapterNumbering.
                    // A style never inherits numbering from Chapter Numbering.
                    if (rSI.m_nLFOIndex != USHRT_MAX)
                    {
                        const SwNumRule* pNumRule = m_vColl[rSI.m_nBase].m_pOutlineNumrule;
                        if (pNumRule)
                            aPreventUseAsChapterNumbering[pNumRule] = true;
                    }
                }
                if (rSI.m_nListLevel == MAXLEVEL)
                    rSI.m_nListLevel = m_vColl[rSI.m_nBase].m_nListLevel;
                if (rSI.mnWW8OutlineLevel == MAXLEVEL)
                    rSI.mnWW8OutlineLevel = m_vColl[rSI.m_nBase].mnWW8OutlineLevel;
                bReRegister = true;
            }

            // Undefined listLevel is treated as the first level with valid numbering rule.
            if (rSI.m_nLFOIndex < USHRT_MAX && rSI.m_nListLevel == MAXLEVEL)
            {
                rSI.m_nListLevel = 0;
                bReRegister = true;
            }

            if (bReRegister)
                RegisterNumFormatOnStyle(nStyle);

            ++nStyle; // increment before the first "continue";

            if (!rSI.m_bColl || !rSI.IsWW8BuiltInHeadingStyle() || !rSI.HasWW8OutlineLevel())
            {
                continue;
            }

            // When ANYTHING is wrong or strange, prohibit eligibility for ChapterNumbering.
            if (rSI.IsOutlineNumbered() && rSI.m_nListLevel != rSI.mnWW8OutlineLevel)
            {
                aPreventUseAsChapterNumbering[rSI.m_pOutlineNumrule] = true;
            }

            aWW8BuiltInHeadingStyles.push_back(&rSI);

            const SwNumRule* pWW8ListStyle = rSI.GetOutlineNumrule();
            if (pWW8ListStyle != nullptr)
            {
                std::map<const SwNumRule*, int>::iterator aCountIter
                    = aWW8ListStyleCounts.find(pWW8ListStyle);
                if (aCountIter == aWW8ListStyleCounts.end())
                {
                    aWW8ListStyleCounts[pWW8ListStyle] = 1;
                }
                else
                {
                    ++(aCountIter->second);
                }
            }
        }

        int nCurrentMaxCount = 0;
        for (const auto& rEntry : aWW8ListStyleCounts)
        {
            if (aPreventUseAsChapterNumbering[rEntry.first])
                continue;

            if (rEntry.second > nCurrentMaxCount)
            {
                nCurrentMaxCount = rEntry.second;
                m_pChosenWW8OutlineStyle = rEntry.first;
            }
        }
    }

    // - set list level properties of Outline Style - ODF's list style applied
    // by default to headings
    // - assign corresponding Heading Paragraph Styles to the Outline Style
    // - If a heading Paragraph Styles is not applying the WW8 list style which
    // had been chosen as
    //   the one which provides the list level properties for the Outline Style,
    // its assignment to
    //   the Outline Style is removed. A potential applied WW8 list style is
    // assigned directly and
    //   its default outline level is applied.
    SwNumRule aOutlineRule(*m_rDoc.GetOutlineNumRule());
    if (m_pChosenWW8OutlineStyle)
    {
        for (int i = 0; i < WW8ListManager::nMaxLevel; ++i)
        {
            // Don't clobber existing outline levels.
            const sal_uInt16 nLevel = 1 << i;
            if (!(nOutlineStyleListLevelWithAssignment & nLevel))
                aOutlineRule.Set(i, m_pChosenWW8OutlineStyle->Get(i));
        }
    }

    for (const SwWW8StyInf* pStyleInf : aWW8BuiltInHeadingStyles)
    {
        const sal_uInt16 nOutlineStyleListLevelOfWW8BuiltInHeadingStyle
            = 1 << pStyleInf->mnWW8OutlineLevel;
        if (nOutlineStyleListLevelOfWW8BuiltInHeadingStyle
            & nOutlineStyleListLevelWithAssignment)
        {
            continue;
        }

        // in case that there are more styles on this level ignore them
        nOutlineStyleListLevelWithAssignment
            |= nOutlineStyleListLevelOfWW8BuiltInHeadingStyle;

        SwTextFormatColl* pTextFormatColl = static_cast<SwTextFormatColl*>(pStyleInf->m_pFormat);
        if (pStyleInf->GetOutlineNumrule() != m_pChosenWW8OutlineStyle
            || (pStyleInf->m_nListLevel < WW8ListManager::nMaxLevel
                && pStyleInf->mnWW8OutlineLevel != pStyleInf->m_nListLevel))
        {
            // WW8 Built-In Heading Style does not apply the chosen one.
            // --> delete assignment to OutlineStyle, but keep its current
            // outline level
            pTextFormatColl->DeleteAssignmentToListLevelOfOutlineStyle();
            // Apply existing WW8 list style a normal list style at the
            // Paragraph Style
            if (pStyleInf->GetOutlineNumrule() != nullptr)
            {
                pTextFormatColl->SetFormatAttr(
                    SwNumRuleItem(pStyleInf->GetOutlineNumrule()->GetName()));
            }
            // apply default outline level of WW8 Built-in Heading Style
            const sal_uInt8 nOutlineLevel
                = SwWW8StyInf::WW8OutlineLevelToOutlinelevel(
                    pStyleInf->mnWW8OutlineLevel);
            pTextFormatColl->SetFormatAttr(
                SfxUInt16Item(RES_PARATR_OUTLINELEVEL, nOutlineLevel));
        }
        else
        {
            pTextFormatColl->AssignToListLevelOfOutlineStyle(
                pStyleInf->mnWW8OutlineLevel);
        }
    }

    if (m_pChosenWW8OutlineStyle)
    {
        m_rDoc.SetOutlineNumRule(aOutlineRule);
    }
}

const OUString* SwWW8ImplReader::GetAnnotationAuthor(sal_uInt16 nIdx)
{
    if (!m_xAtnNames && m_xWwFib->m_lcbGrpStAtnOwners)
    {
        // Determine authors: can be found in the TableStream
        m_xAtnNames.emplace();
        SvStream& rStrm = *m_pTableStream;

        auto nOldPos = rStrm.Tell();
        bool bValidPos = checkSeek(rStrm, m_xWwFib->m_fcGrpStAtnOwners);
        if (bValidPos)
        {
            tools::Long nRead = 0, nCount = m_xWwFib->m_lcbGrpStAtnOwners;
            while (nRead < nCount && rStrm.good())
            {
                if( m_bVer67 )
                {
                    m_xAtnNames->push_back(read_uInt8_PascalString(rStrm,
                        RTL_TEXTENCODING_MS_1252));
                    nRead += m_xAtnNames->rbegin()->getLength() + 1; // Length + sal_uInt8 count
                }
                else
                {
                    m_xAtnNames->push_back(read_uInt16_PascalString(rStrm));
                    // Unicode: double the length + sal_uInt16 count
                    nRead += (m_xAtnNames->rbegin()->getLength() + 1)*2;
                }
            }
        }
        rStrm.Seek( nOldPos );
    }

    const OUString *pRet = nullptr;
    if (m_xAtnNames && nIdx < m_xAtnNames->size())
        pRet = &((*m_xAtnNames)[nIdx]);
    return pRet;
}

void SwWW8ImplReader::GetSmartTagInfo(SwFltRDFMark& rMark)
{
    if (!m_pSmartTagData && m_xWwFib->m_lcbFactoidData)
    {
        m_pSmartTagData.reset(new WW8SmartTagData);
        m_pSmartTagData->Read(*m_pTableStream, m_xWwFib->m_fcFactoidData, m_xWwFib->m_lcbFactoidData);
    }

    if (!m_pSmartTagData)
        return;

    // Check if the handle is a valid smart tag bookmark index.
    size_t nIndex = rMark.GetHandle();
    if (nIndex >= m_pSmartTagData->m_aPropBags.size())
        return;

    // Check if the smart tag bookmark refers to a valid factoid type.
    const MSOPropertyBag& rPropertyBag = m_pSmartTagData->m_aPropBags[rMark.GetHandle()];
    auto& rFactoidTypes = m_pSmartTagData->m_aPropBagStore.m_aFactoidTypes;
    auto itPropertyBag = std::find_if(rFactoidTypes.begin(), rFactoidTypes.end(),
        [&rPropertyBag](const MSOFactoidType& rType) { return rType.m_nId == rPropertyBag.m_nId; });
    if (itPropertyBag == rFactoidTypes.end())
        return;

    // Check if the factoid is an RDF one.
    const MSOFactoidType& rFactoidType = *itPropertyBag;
    if (rFactoidType.m_aUri != "http://www.w3.org/1999/02/22-rdf-syntax-ns#")
        return;

    // Finally put the relevant attributes to the mark.
    std::vector< std::pair<OUString, OUString> > aAttributes;
    for (const MSOProperty& rProperty : rPropertyBag.m_aProperties)
    {
        OUString aKey;
        OUString aValue;
        if (rProperty.m_nKey < m_pSmartTagData->m_aPropBagStore.m_aStringTable.size())
            aKey = m_pSmartTagData->m_aPropBagStore.m_aStringTable[rProperty.m_nKey];
        if (rProperty.m_nValue < m_pSmartTagData->m_aPropBagStore.m_aStringTable.size())
            aValue = m_pSmartTagData->m_aPropBagStore.m_aStringTable[rProperty.m_nValue];
        if (!aKey.isEmpty() && !aValue.isEmpty())
            aAttributes.emplace_back(aKey, aValue);
    }
    rMark.SetAttributes(std::move(aAttributes));
}

ErrCode SwWW8ImplReader::LoadDoc(WW8Glossary *pGloss)
{
    ErrCode nErrRet = ERRCODE_NONE;

    {
        static constexpr OUString aNames[ 13 ] = {
            u"WinWord/WW"_ustr, u"WinWord/WW8"_ustr, u"WinWord/WWFT"_ustr,
            u"WinWord/WWFLX"_ustr, u"WinWord/WWFLY"_ustr,
            u"WinWord/WWF"_ustr,
            u"WinWord/WWFA0"_ustr, u"WinWord/WWFA1"_ustr, u"WinWord/WWFA2"_ustr,
            u"WinWord/WWFB0"_ustr, u"WinWord/WWFB1"_ustr, u"WinWord/WWFB2"_ustr,
            u"WinWord/RegardHindiDigits"_ustr
        };
        sal_uInt64 aVal[ 13 ];

        SwFilterOptions aOpt( 13, aNames, aVal );

        m_nIniFlags = aVal[ 0 ];
        m_nIniFlags1= aVal[ 1 ];
        // Moves Flys by x twips to the right or left
        m_nIniFlyDx = aVal[ 3 ];
        m_nIniFlyDy = aVal[ 4 ];

        m_nFieldFlags = aVal[ 5 ];
        m_nFieldTagAlways[0] = aVal[ 6 ];
        m_nFieldTagAlways[1] = aVal[ 7 ];
        m_nFieldTagAlways[2] = aVal[ 8 ];
        m_nFieldTagBad[0] = aVal[ 9 ];
        m_nFieldTagBad[1] = aVal[ 10 ];
        m_nFieldTagBad[2] = aVal[ 11 ];
        m_bRegardHindiDigits = aVal[ 12 ] > 0;
    }

    sal_uInt16 nMagic(0);
    m_pStrm->ReadUInt16( nMagic );

    // Remember: 6 means "6 OR 7", 7 means "JUST 7"
    switch (m_nWantedVersion)
    {
        case 6:
        case 7:
            if (
                 0xa59b != nMagic && 0xa59c != nMagic &&
                 0xa5dc != nMagic && 0xa5db != nMagic &&
                 (nMagic < 0xa697 || nMagic > 0xa699)
               )
            {
                // Test for own 97 fake!
                if (m_pStg && 0xa5ec == nMagic)
                {
                    sal_uInt64 nCurPos = m_pStrm->Tell();
                    if (checkSeek(*m_pStrm, nCurPos + 2))
                    {
                        sal_uInt32 nfcMin(0);
                        m_pStrm->ReadUInt32( nfcMin );
                        if (0x300 != nfcMin)
                            nErrRet = ERR_WW6_NO_WW6_FILE_ERR;
                    }
                    m_pStrm->Seek( nCurPos );
                }
                else
                    nErrRet = ERR_WW6_NO_WW6_FILE_ERR;
            }
            break;
        case 8:
            if (0xa5ec != nMagic)
                nErrRet = ERR_WW8_NO_WW8_FILE_ERR;
            break;
        default:
            nErrRet = ERR_WW8_NO_WW8_FILE_ERR;
            OSL_ENSURE( false, "We forgot to encode nVersion!" );
            break;
    }

    if (!nErrRet)
        nErrRet = LoadThroughDecryption(pGloss);

    m_rDoc.PropagateOutlineRule();

    return nErrRet;
}

extern "C" SAL_DLLPUBLIC_EXPORT Reader* ImportDOC()
{
    return new WW8Reader;
}

namespace
{
    class FontCacheGuard
    {
    public:
        ~FontCacheGuard()
        {
            FlushFontCache();
        }
    };
}

bool TestImportDOC(SvStream &rStream, const OUString &rFltName)
{
    FontCacheGuard aFontCacheGuard;
    std::unique_ptr<Reader> xReader(ImportDOC());

    rtl::Reference<SotStorage> xStorage;
    xReader->m_pStream = &rStream;
    if (rFltName != "WW6")
    {
        try
        {
            xStorage.set(new SotStorage(rStream));
            if (xStorage->GetError())
                return false;
        }
        catch (...)
        {
            return false;
        }
        xReader->m_pStorage = xStorage.get();
    }
    xReader->SetFltName(rFltName);

    SwGlobals::ensure();

    SfxObjectShellLock xDocSh(new SwDocShell(SfxObjectCreateMode::INTERNAL));
    xDocSh->DoInitNew();
    SwDoc *pD =  static_cast<SwDocShell*>((&xDocSh))->GetDoc();

    SwPaM aPaM(pD->GetNodes().GetEndOfContent(), SwNodeOffset(-1));
    pD->SetInReading(true);
    bool bRet = xReader->Read(*pD, OUString(), aPaM, OUString()) == ERRCODE_NONE;
    pD->SetInReading(false);

    return bRet;
}

extern "C" SAL_DLLPUBLIC_EXPORT bool TestImportWW8(SvStream &rStream)
{
    return TestImportDOC(rStream, u"CWW8"_ustr);
}

extern "C" SAL_DLLPUBLIC_EXPORT bool TestImportWW6(SvStream &rStream)
{
    return TestImportDOC(rStream, u"CWW6"_ustr);
}

extern "C" SAL_DLLPUBLIC_EXPORT bool TestImportWW2(SvStream &rStream)
{
    return TestImportDOC(rStream, u"WW6"_ustr);
}

ErrCode WW8Reader::OpenMainStream(rtl::Reference<SotStorageStream>& rRef, sal_uInt16& rBuffSize)
{
    ErrCode nRet = ERR_SWG_READ_ERROR;
    OSL_ENSURE(m_pStorage, "Where is my Storage?");
    rRef = m_pStorage->OpenSotStream( u"WordDocument"_ustr, StreamMode::READ | StreamMode::SHARE_DENYALL);

    if( rRef.is() )
    {
        if( ERRCODE_NONE == rRef->GetError() )
        {
            sal_uInt16 nOld = rRef->GetBufferSize();
            rRef->SetBufferSize( rBuffSize );
            rBuffSize = nOld;
            nRet = ERRCODE_NONE;
        }
        else
            nRet = rRef->GetError();
    }
    return nRet;
}

static void lcl_getListOfStreams(SotStorage * pStorage, comphelper::SequenceAsHashMap& aStreamsData, std::u16string_view sPrefix)
{
    SvStorageInfoList aElements;
    pStorage->FillInfoList(&aElements);
    for (const auto & aElement : aElements)
    {
        OUString sStreamFullName = sPrefix.size() ? OUString::Concat(sPrefix) + "/" + aElement.GetName() : aElement.GetName();
        if (aElement.IsStorage())
        {
            rtl::Reference<SotStorage> xSubStorage = pStorage->OpenSotStorage(aElement.GetName(), StreamMode::STD_READ | StreamMode::SHARE_DENYALL);
            lcl_getListOfStreams(xSubStorage.get(), aStreamsData, sStreamFullName);
        }
        else
        {
            // Read stream
            rtl::Reference<SotStorageStream> rStream = pStorage->OpenSotStream(aElement.GetName(), StreamMode::READ | StreamMode::SHARE_DENYALL);
            if (rStream.is())
            {
                sal_Int32 nStreamSize = rStream->GetSize();
                css::uno::Sequence< sal_Int8 > oData;
                oData.realloc(nStreamSize);
                sal_Int32 nReadBytes = rStream->ReadBytes(oData.getArray(), nStreamSize);
                if (nStreamSize == nReadBytes)
                    aStreamsData[sStreamFullName] <<= oData;
            }
        }
    }
}

ErrCode WW8Reader::DecryptDRMPackage()
{
    // We have DRM encrypted storage. We should try to decrypt it first, if we can
    uno::Sequence< uno::Any > aArguments;
    const uno::Reference<uno::XComponentContext>& xComponentContext(comphelper::getProcessComponentContext());
    uno::Reference< packages::XPackageEncryption > xPackageEncryption(
        xComponentContext->getServiceManager()->createInstanceWithArgumentsAndContext(
            u"com.sun.star.comp.oox.crypto.DRMDataSpace"_ustr, aArguments, xComponentContext), uno::UNO_QUERY);

    if (!xPackageEncryption.is())
    {
        // We do not know how to decrypt this
        return ERRCODE_IO_ACCESSDENIED;
    }

    comphelper::SequenceAsHashMap aStreamsData;
    lcl_getListOfStreams(m_pStorage.get(), aStreamsData, u"");

    try {
        uno::Sequence<beans::NamedValue> aStreams = aStreamsData.getAsConstNamedValueList();
        if (!xPackageEncryption->readEncryptionInfo(aStreams))
        {
            // We failed with decryption
            return ERRCODE_IO_ACCESSDENIED;
        }

        rtl::Reference<SotStorageStream> rContentStream = m_pStorage->OpenSotStream(u"\011DRMContent"_ustr, StreamMode::READ | StreamMode::SHARE_DENYALL);
        if (!rContentStream.is())
        {
            return ERRCODE_IO_NOTEXISTS;
        }

        mDecodedStream = std::make_shared<SvMemoryStream>();

        uno::Reference<io::XInputStream > xInputStream(new utl::OSeekableInputStreamWrapper(rContentStream.get(), false));
        uno::Reference<io::XOutputStream > xDecryptedStream(new utl::OSeekableOutputStreamWrapper(*mDecodedStream));

        if (!xPackageEncryption->decrypt(xInputStream, xDecryptedStream))
        {
            // We failed with decryption
            return ERRCODE_IO_ACCESSDENIED;
        }

        mDecodedStream->Seek(0);

        // Further reading is done from new document
        m_pStorage = new SotStorage(*mDecodedStream);

        // Set the media descriptor data
        uno::Sequence<beans::NamedValue> aEncryptionData = xPackageEncryption->createEncryptionData(u""_ustr);
        m_pMedium->GetItemSet().Put(SfxUnoAnyItem(SID_ENCRYPTIONDATA, uno::Any(aEncryptionData)));
    }
    catch (const std::exception&)
    {
        return ERRCODE_IO_ACCESSDENIED;
    }

    return ERRCODE_NONE;
}

ErrCodeMsg WW8Reader::Read(SwDoc &rDoc, const OUString& rBaseURL, SwPaM &rPaM, const OUString & /* FileName */)
{
    sal_uInt16 nOldBuffSize = 32768;
    bool bNew = !m_bInsertMode; // New Doc (no inserting)

    rtl::Reference<SotStorageStream> refStrm; // So that no one else can steal the Stream
    SvStream* pIn = m_pStream;

    ErrCode nRet = ERRCODE_NONE;
    sal_uInt8 nVersion = 8;

    const OUString sFltName = GetFltName();
    if ( sFltName=="WW6" )
    {
        if (m_pStream)
            nVersion = 6;
        else
        {
            OSL_ENSURE(false, "WinWord 95 Reader-Read without Stream");
            nRet = ERR_SWG_READ_ERROR;
        }
    }
    else
    {
        if ( sFltName=="CWW6" )
            nVersion = 6;
        else if ( sFltName=="CWW7" )
            nVersion = 7;

        if( m_pStorage.is() )
        {
            // Check if we have special encrypted content
            rtl::Reference<SotStorageStream> rRef = m_pStorage->OpenSotStream(u"\006DataSpaces/DataSpaceInfo/\011DRMDataSpace"_ustr, StreamMode::READ | StreamMode::SHARE_DENYALL);
            if (rRef.is())
            {
                nRet = DecryptDRMPackage();
            }
            nRet = OpenMainStream(refStrm, nOldBuffSize);
            pIn = refStrm.get();
        }
        else
        {
            OSL_ENSURE(false, "WinWord 95/97 Reader-Read without Storage");
            nRet = ERR_SWG_READ_ERROR;
        }
    }

    if( !nRet )
    {
        std::unique_ptr<SwWW8ImplReader> pRdr(new SwWW8ImplReader(nVersion, m_pStorage.get(), pIn, rDoc,
            rBaseURL, bNew, m_bSkipImages, *rPaM.GetPoint()));
        if (bNew)
        {
            rPaM.GetBound().nContent.Assign(nullptr, 0);
            rPaM.GetBound(false).nContent.Assign(nullptr, 0);
        }
        try
        {
            nRet = pRdr->LoadDoc();
        }
        catch( const std::exception& )
        {
            nRet = ERR_WW8_NO_WW8_FILE_ERR;
        }

        if( refStrm.is() )
        {
            refStrm->SetBufferSize( nOldBuffSize );
            refStrm.clear();
        }
        else
        {
            pIn->ResetError();
        }

    }
    return nRet;
}

SwReaderType WW8Reader::GetReaderType()
{
    return SwReaderType::Storage | SwReaderType::Stream;
}

bool WW8Reader::HasGlossaries() const
{
    return true;
}

bool WW8Reader::ReadGlossaries(SwTextBlocks& rBlocks, bool bSaveRelFiles) const
{
    bool bRet=false;

    WW8Reader *pThis = const_cast<WW8Reader *>(this);

    sal_uInt16 nOldBuffSize = 32768;
    rtl::Reference<SotStorageStream> refStrm;
    if (!pThis->OpenMainStream(refStrm, nOldBuffSize))
    {
        WW8Glossary aGloss( refStrm, 8, m_pStorage.get() );
        bRet = aGloss.Load( rBlocks, bSaveRelFiles );
    }
    return bRet;
}

bool SwMSDffManager::GetOLEStorageName(sal_uInt32 nOLEId, OUString& rStorageName,
    rtl::Reference<SotStorage>& rSrcStorage, uno::Reference < embed::XStorage >& rDestStorage) const
{
    bool bRet = false;

    sal_Int32 nPictureId = 0;
    if (m_rReader.m_pStg)
    {
        // Via the TextBox-PLCF we get the right char Start-End positions
        // We should then find the EmbeddedField and the corresponding Sprms
        // in that Area.
        // We only need the Sprm for the Picture Id.
        sal_uInt64 nOldPos = m_rReader.m_pStrm->Tell();
        {
            // #i32596# - consider return value of method
            // <rReader.GetTxbxTextSttEndCp(..)>. If it returns false, method
            // wasn't successful. Thus, continue in this case.
            // Note: Ask MM for initialization of <nStartCp> and <nEndCp>.
            // Note: Ask MM about assertions in method <rReader.GetTxbxTextSttEndCp(..)>.
            WW8_CP nStartCp, nEndCp;
            if ( m_rReader.m_bDrawCpOValid && m_rReader.GetTxbxTextSttEndCp(nStartCp, nEndCp,
                            o3tl::narrowing<sal_uInt16>((nOLEId >> 16) & 0xFFFF),
                            o3tl::narrowing<sal_uInt16>(nOLEId & 0xFFFF)) )
            {
                WW8PLCFxSaveAll aSave;
                m_rReader.m_xPlcxMan->SaveAllPLCFx( aSave );

                nStartCp += m_rReader.m_nDrawCpO;
                nEndCp   += m_rReader.m_nDrawCpO;
                WW8PLCFx_Cp_FKP* pChp = m_rReader.m_xPlcxMan->GetChpPLCF();
                wwSprmParser aSprmParser(*m_rReader.m_xWwFib);
                while (nStartCp <= nEndCp && !nPictureId)
                {
                    if (!pChp->SeekPos( nStartCp))
                        break;
                    WW8PLCFxDesc aDesc;
                    pChp->GetSprms( &aDesc );

                    if (aDesc.nSprmsLen && aDesc.pMemPos) // Attributes present
                    {
                        auto nLen = aDesc.nSprmsLen;
                        const sal_uInt8* pSprm = aDesc.pMemPos;

                        while (nLen >= 2 && !nPictureId)
                        {
                            sal_uInt16 nId = aSprmParser.GetSprmId(pSprm);
                            sal_Int32 nSL = aSprmParser.GetSprmSize(nId, pSprm, nLen);

                            if( nLen < nSL )
                                break; // Not enough Bytes left

                            if (0x6A03 == nId)
                            {
                                nPictureId = SVBT32ToUInt32(pSprm +
                                    aSprmParser.DistanceToData(nId));
                                bRet = true;
                            }
                            pSprm += nSL;
                            nLen -= nSL;
                        }
                    }
                    nStartCp = aDesc.nEndPos;
                }

                m_rReader.m_xPlcxMan->RestoreAllPLCFx( aSave );
            }
        }
        m_rReader.m_pStrm->Seek( nOldPos );
    }

    if( bRet )
    {
        rStorageName = "_";
        rStorageName += OUString::number(nPictureId);
        rSrcStorage = m_rReader.m_pStg->OpenSotStorage(SL::aObjectPool);
        if (!m_rReader.m_pDocShell)
            bRet=false;
        else
            rDestStorage = m_rReader.m_pDocShell->GetStorage();
    }
    return bRet;
}

/**
 * When reading a single Box (which possibly is part of a group), we do
 * not yet have enough information to decide whether we need it as a TextField
 * or not.
 * So convert all of them as a precaution.
 * FIXME: Actually implement this!
 */
bool SwMSDffManager::ShapeHasText(sal_uLong, sal_uLong) const
{
    return true;
}

bool SwWW8ImplReader::InEqualOrHigherApo(int nLvl) const
{
    if (nLvl)
        --nLvl;
    // #i60827# - check size of <maApos> to assure that <maApos.begin() + nLvl> can be performed.
    if ( sal::static_int_cast< sal_Int32>(nLvl) >= sal::static_int_cast< sal_Int32>(m_aApos.size()) )
    {
        return false;
    }
    auto aIter = std::find(m_aApos.begin() + nLvl, m_aApos.end(), true);
    return aIter != m_aApos.end();
}

bool SwWW8ImplReader::InEqualApo(int nLvl) const
{
    // If we are in a table, see if an apo was inserted at the level below the table.
    if (nLvl)
        --nLvl;
    if (nLvl < 0 || o3tl::make_unsigned(nLvl) >= m_aApos.size())
        return false;
    return m_aApos[nLvl];
}

namespace sw::hack
{
        Position::Position(const SwPosition &rPos)
            : maPtNode(rPos.GetNode()), mnPtContent(rPos.GetContentIndex())
        {
        }

        Position::operator SwPosition() const
        {
            return SwPosition(maPtNode, maPtNode.GetNode().GetContentNode(), mnPtContent);
        }
}

SwMacroInfo::SwMacroInfo()
    : SdrObjUserData( SdrInventor::ScOrSwDraw, SW_UD_IMAPDATA )
    , mnShapeId(-1)
{
}

SwMacroInfo::~SwMacroInfo()
{
}

std::unique_ptr<SdrObjUserData> SwMacroInfo::Clone( SdrObject* /*pObj*/ ) const
{
   return std::unique_ptr<SdrObjUserData>(new SwMacroInfo( *this ));
}

std::unique_ptr<SfxItemSet> SwWW8ImplReader::SetCurrentItemSet(std::unique_ptr<SfxItemSet> pItemSet)
{
    std::unique_ptr<SfxItemSet> xRet(std::move(m_xCurrentItemSet));
    m_xCurrentItemSet = std::move(pItemSet);
    return xRet;
}

void SwWW8ImplReader::NotifyMacroEventRead()
{
    if (m_bNotifyMacroEventRead)
        return;
    if (SwDocShell* pShell = m_rDoc.GetDocShell())
    {
        comphelper::DocumentInfo::notifyMacroEventRead(pShell->GetModel());
        m_bNotifyMacroEventRead = true;
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
