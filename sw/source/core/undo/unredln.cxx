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

#include <UndoRedline.hxx>
#include <hintids.hxx>
#include <osl/diagnose.h>
#include <unotools/charclass.hxx>
#include <doc.hxx>
#include <IDocumentRedlineAccess.hxx>
#include <swundo.hxx>
#include <pam.hxx>
#include <ndtxt.hxx>
#include <txtfrm.hxx>
#include <mvsave.hxx>
#include <rolbck.hxx>
#include <UndoCore.hxx>
#include <UndoDelete.hxx>
#include <strings.hrc>
#include <redline.hxx>
#include <docary.hxx>
#include <sortopt.hxx>
#include <docedt.hxx>

SwUndoRedline::SwUndoRedline( SwUndoId nUsrId, const SwPaM& rRange, sal_Int8 nDepth, bool bHierarchical )
    : SwUndo( SwUndoId::REDLINE, rRange.GetDoc() ), SwUndRng( rRange ),
    mnUserId( nUsrId ),
    mbHiddenRedlines( false ),
    mnDepth( nDepth ),
    mbHierarchical(bHierarchical)
{
    // consider Redline
    SwDoc& rDoc = rRange.GetDoc();
    if( rDoc.getIDocumentRedlineAccess().IsRedlineOn() )
    {
        switch( mnUserId )
        {
        case SwUndoId::DELETE:
        case SwUndoId::REPLACE:
            mpRedlData.reset( new SwRedlineData( RedlineType::Delete, rDoc.getIDocumentRedlineAccess().GetRedlineAuthor() ) );
            break;
        default:
            ;
        }
        SetRedlineFlags( rDoc.getIDocumentRedlineAccess().GetRedlineFlags() );
    }

    SwNodeOffset nEndExtra = rDoc.GetNodes().GetEndOfExtras().GetIndex();

    mpRedlSaveData.reset( new SwRedlineSaveDatas );
    bool bCopyNext = SwUndoId::REJECT_REDLINE != mnUserId;
    if (mbHierarchical)
    {
        bCopyNext = true;
    }
    if( !FillSaveData( rRange, *mpRedlSaveData, false, bCopyNext ))
    {
        mpRedlSaveData.reset();
    }
    else
    {
        mbHiddenRedlines = HasHiddenRedlines( *mpRedlSaveData );
        if( mbHiddenRedlines )           // then the NodeIndices of SwUndRng need to be corrected
        {
            nEndExtra -= rDoc.GetNodes().GetEndOfExtras().GetIndex();
            m_nSttNode -= nEndExtra;
            m_nEndNode -= nEndExtra;
        }
    }
}

SwUndoRedline::~SwUndoRedline()
{
    mpRedlData.reset();
    mpRedlSaveData.reset();
}

sal_uInt16 SwUndoRedline::GetRedlSaveCount() const
{
    return mpRedlSaveData ? mpRedlSaveData->size() : 0;
}

#if OSL_DEBUG_LEVEL > 0
void SwUndoRedline::SetRedlineCountDontCheck(bool bCheck)
{
    if (mpRedlSaveData)
        mpRedlSaveData->SetRedlineCountDontCheck(bCheck);
}
#endif

void SwUndoRedline::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SwUndoRedline"));
    (void)xmlTextWriterWriteFormatAttribute(pWriter, BAD_CAST("ptr"), "%p", this);
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("symbol"), BAD_CAST(typeid(*this).name()));

    const SwRedlineData* pRedlData = mpRedlData.get();
    while (pRedlData)
    {
        pRedlData->dumpAsXml(pWriter);
        pRedlData = pRedlData->Next();
    }

    if (mpRedlSaveData)
    {
        mpRedlSaveData->dumpAsXml(pWriter);
    }

    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("hierarchical"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("value"),
                                BAD_CAST(OString::boolean(mbHierarchical).getStr()));
    (void)xmlTextWriterEndElement(pWriter);
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("depth"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("value"),
                                BAD_CAST(OString::number(mnDepth).getStr()));
    (void)xmlTextWriterEndElement(pWriter);

    (void)xmlTextWriterEndElement(pWriter);
}

void SwUndoRedline::UndoImpl(::sw::UndoRedoContext & rContext)
{
    SwDoc& rDoc = rContext.GetDoc();
    SwPaM& rPam(AddUndoRedoPaM(rContext));

    // fix PaM for deletions shown in margin
    bool bIsDeletion = dynamic_cast<SwUndoRedlineDelete*>(this);
    const SwRedlineTable& rTable = rDoc.getIDocumentRedlineAccess().GetRedlineTable();
    sal_uInt32 nMaxId = SAL_MAX_UINT32;
    if ( bIsDeletion && rTable.size() > 0 )
    {
        // Nodes of the deletion range are in the newest invisible redlines.
        // Set all redlines visible and recover the original deletion range.
        for (SwNodeOffset nNodes(0); nNodes <  m_nEndNode - m_nSttNode + 1; ++nNodes)
        {
            SwRedlineTable::size_type nCurRedlinePos = 0;
            SwRangeRedline * pRedline(rTable[nCurRedlinePos]);

            // search last but nNodes redline by its nth biggest id
            for( SwRedlineTable::size_type n = 1; n < rTable.size(); ++n )
            {
                SwRangeRedline *pRed(rTable[n]);
                if ( !pRed->HasMark() && pRedline->GetId() < pRed->GetId() && pRed->GetId() < nMaxId )
                {
                    nCurRedlinePos = n;
                    pRedline = pRed;
                }
            }

            nMaxId = pRedline->GetId();

            if ( !pRedline->IsVisible() && !pRedline->HasMark() )
            {
                // set it visible
                pRedline->Show(0, rTable.GetPos(pRedline), /*bForced=*/true);
                pRedline->Show(1, rTable.GetPos(pRedline), /*bForced=*/true);

                // extend the range
                if ( nNodes == SwNodeOffset(0) )
                    rPam = *pRedline;
                else
                {
                    rPam.SetMark();
                    *rPam.GetMark() = *pRedline->GetMark();
                }
            }
        }
    }

    UndoRedlineImpl(rDoc, rPam);

    if( mpRedlSaveData )
    {
        SwNodeOffset nEndExtra = rDoc.GetNodes().GetEndOfExtras().GetIndex();
        SetSaveData(rDoc, *mpRedlSaveData);
        if( mbHiddenRedlines )
        {
            mpRedlSaveData->clear();

            nEndExtra = rDoc.GetNodes().GetEndOfExtras().GetIndex() - nEndExtra;
            m_nSttNode += nEndExtra;
            m_nEndNode += nEndExtra;
        }
        SetPaM(rPam, true);
    }

    // update frames after calling SetSaveData
    if ( bIsDeletion )
    {
        sw::UpdateFramesForRemoveDeleteRedline(rDoc, rPam);
    }
    else if (dynamic_cast<SwUndoAcceptRedline*>(this)
          || dynamic_cast<SwUndoRejectRedline*>(this))
    {   // (can't check here if there's a delete redline being accepted)
        sw::UpdateFramesForAddDeleteRedline(rDoc, rPam);
    }
}

void SwUndoRedline::RedoImpl(::sw::UndoRedoContext & rContext)
{
    SwDoc& rDoc = rContext.GetDoc();
    RedlineFlags eOld = rDoc.getIDocumentRedlineAccess().GetRedlineFlags();
    rDoc.getIDocumentRedlineAccess().SetRedlineFlags_intern(( eOld & ~RedlineFlags::Ignore) | RedlineFlags::On );

    SwPaM & rPam( AddUndoRedoPaM(rContext) );
    if( mpRedlSaveData && mbHiddenRedlines )
    {
        SwNodeOffset nEndExtra = rDoc.GetNodes().GetEndOfExtras().GetIndex();
        FillSaveData(rPam, *mpRedlSaveData, false, SwUndoId::REJECT_REDLINE != mnUserId );

        nEndExtra -= rDoc.GetNodes().GetEndOfExtras().GetIndex();
        m_nSttNode -= nEndExtra;
        m_nEndNode -= nEndExtra;
    }

    RedoRedlineImpl(rDoc, rPam);

    SetPaM(rPam, true);
    rDoc.getIDocumentRedlineAccess().SetRedlineFlags_intern( eOld );
}

void SwUndoRedline::UndoRedlineImpl(SwDoc &, SwPaM &)
{
}

// default: remove redlines
void SwUndoRedline::RedoRedlineImpl(SwDoc & rDoc, SwPaM & rPam)
{
    rDoc.getIDocumentRedlineAccess().DeleteRedline(rPam, true, RedlineType::Any);
}

SwUndoRedlineDelete::SwUndoRedlineDelete(
        const SwPaM& rRange, SwUndoId const nUsrId, SwDeleteFlags const flags)
    : SwUndoRedline( nUsrId != SwUndoId::EMPTY ? nUsrId : SwUndoId::DELETE, rRange ),
    m_bCanGroup( false ), m_bIsDelim( false ), m_bIsBackspace( false )
{
    const SwTextNode* pTNd;
    SetRedlineText(rRange.GetText());
    if( SwUndoId::DELETE == mnUserId &&
        m_nSttNode == m_nEndNode && m_nSttContent + 1 == m_nEndContent &&
        nullptr != (pTNd = rRange.GetPointNode().GetTextNode()) )
    {
        sal_Unicode const cCh = pTNd->GetText()[m_nSttContent];
        if( CH_TXTATR_BREAKWORD != cCh && CH_TXTATR_INWORD != cCh )
        {
            m_bCanGroup = true;
            m_bIsDelim = !GetAppCharClass().isLetterNumeric( pTNd->GetText(),
                                                            m_nSttContent );
            m_bIsBackspace = m_nSttContent == rRange.GetPoint()->GetContentIndex();
        }
    }

    m_bCacheComment = false;
    if (flags & SwDeleteFlags::ArtificialSelection)
    {
        InitHistory(rRange);
    }
}

void SwUndoRedlineDelete::InitHistory(SwPaM const& rRedline)
{
    m_pHistory.reset(new SwHistory);
    // try to rely on direction of rPam here so it works for
    // backspacing/deleting consecutive characters
    SaveFlyArr flys;
    SaveFlyInRange(rRedline, *rRedline.GetMark(), flys, false, m_pHistory.get());
    RestFlyInRange(flys, *rRedline.GetPoint(), &rRedline.GetPoint()->GetNode(), true);
    if (m_pHistory->Count())
    {
        m_bCanGroup = false; // how to group history?
    }
}

// bit of a hack, replace everything...
SwRewriter SwUndoRedlineDelete::GetRewriter() const
{
    SwRewriter aResult;
    OUString aStr = DenoteSpecialCharacters(m_sRedlineText);
    aStr = ShortenString(aStr, nUndoStringLength, SwResId(STR_LDOTS));
    SwRewriter aRewriter;
    aRewriter.AddRule(UndoArg1, aStr);
    OUString ret = aRewriter.Apply(SwResId(STR_UNDO_REDLINE_DELETE));
    aResult.AddRule(UndoArg1, ret);
    return aResult;
}

void SwUndoRedlineDelete::SetRedlineText(const OUString & rText)
{
    m_sRedlineText = rText;
}

void SwUndoRedlineDelete::UndoRedlineImpl(SwDoc & rDoc, SwPaM & rPam)
{
    rDoc.getIDocumentRedlineAccess().DeleteRedline(rPam, true, RedlineType::Any);
    if (m_pHistory)
    {
        m_pHistory->TmpRollback(rDoc, 0);
    }
}

void SwUndoRedlineDelete::RedoRedlineImpl(SwDoc & rDoc, SwPaM & rPam)
{
    if (rPam.GetPoint() != rPam.GetMark())
    {
        if (m_pHistory) // if it was created before, it must be recreated now
        {
            rPam.Normalize(m_bIsBackspace); // to check the correct edge
            InitHistory(rPam);
        }
        rDoc.getIDocumentRedlineAccess().AppendRedline( new SwRangeRedline(*mpRedlData, rPam), false );
    }
    sw::UpdateFramesForAddDeleteRedline(rDoc, rPam);
}

bool SwUndoRedlineDelete::CanGrouping( const SwUndoRedlineDelete& rNext )
{
    bool bRet = false;
    if( SwUndoId::DELETE == mnUserId && mnUserId == rNext.mnUserId &&
        m_bCanGroup && rNext.m_bCanGroup &&
        m_bIsDelim == rNext.m_bIsDelim &&
        m_bIsBackspace == rNext.m_bIsBackspace &&
        m_nSttNode == m_nEndNode &&
        rNext.m_nSttNode == m_nSttNode &&
        rNext.m_nEndNode == m_nEndNode )
    {
        int bIsEnd = 0;
        if( rNext.m_nSttContent == m_nEndContent )
            bIsEnd = 1;
        else if( rNext.m_nEndContent == m_nSttContent )
            bIsEnd = -1;

        if( bIsEnd &&
            (( !mpRedlSaveData && !rNext.mpRedlSaveData ) ||
             ( mpRedlSaveData && rNext.mpRedlSaveData &&
                SwUndo::CanRedlineGroup( *mpRedlSaveData,
                            *rNext.mpRedlSaveData, 1 != bIsEnd )
             )))
        {
            if( 1 == bIsEnd )
                m_nEndContent = rNext.m_nEndContent;
            else
                m_nSttContent = rNext.m_nSttContent;
            bRet = true;
        }
    }
    return bRet;
}

SwUndoRedlineSort::SwUndoRedlineSort( const SwPaM& rRange,
                                    const SwSortOptions& rOpt )
    : SwUndoRedline( SwUndoId::SORT_TXT, rRange ),
    m_pOpt( new SwSortOptions( rOpt ) ),
    m_nSaveEndNode( m_nEndNode ), m_nSaveEndContent( m_nEndContent )
{
}

SwUndoRedlineSort::~SwUndoRedlineSort()
{
}

void SwUndoRedlineSort::UndoRedlineImpl(SwDoc & rDoc, SwPaM & rPam)
{
    // rPam contains the sorted range
    // aSaveRange contains copied (i.e. original) range

    auto [pStart, pEnd] = rPam.StartEnd(); // SwPosition*

    SwNodeIndex aPrevIdx( pStart->GetNode(), -1 );
    SwNodeOffset nOffsetTemp = pEnd->GetNodeIndex() - pStart->GetNodeIndex();

    if( !( RedlineFlags::ShowDelete & rDoc.getIDocumentRedlineAccess().GetRedlineFlags()) )
    {
        // Search both Redline objects and make them visible to make the nodes
        // consistent again. The 'delete' one is hidden, thus search for the
        // 'insert' Redline object. The former is located directly after the latter.
        SwRedlineTable::size_type nFnd = rDoc.getIDocumentRedlineAccess().GetRedlinePos(
                            *rDoc.GetNodes()[ m_nSttNode + 1 ],
                            RedlineType::Insert );
        OSL_ENSURE( SwRedlineTable::npos != nFnd && nFnd+1 < rDoc.getIDocumentRedlineAccess().GetRedlineTable().size(),
                    "could not find an Insert object" );
        ++nFnd;
        rDoc.getIDocumentRedlineAccess().GetRedlineTable()[nFnd]->Show(1, nFnd);
    }

    {
        SwPaM aTmp( rPam.GetMark()->GetNode() );
        aTmp.SetMark();
        aTmp.GetPoint()->Assign( m_nSaveEndNode, m_nSaveEndContent );
        rDoc.getIDocumentRedlineAccess().DeleteRedline( aTmp, true, RedlineType::Any );
    }

    rDoc.getIDocumentContentOperations().DelFullPara(rPam);

    SwPaM *const pPam = & rPam;
    pPam->DeleteMark();
    pPam->GetPoint()->Assign( aPrevIdx.GetNode(), SwNodeOffset(+1) );
    pPam->SetMark();

    pPam->GetPoint()->Adjust(nOffsetTemp);
    SwContentNode* pCNd = pPam->GetPointContentNode();
    pPam->GetPoint()->SetContent( pCNd->Len() );

    SetValues( *pPam );

    SetPaM(rPam);
}

void SwUndoRedlineSort::RedoRedlineImpl(SwDoc & rDoc, SwPaM & rPam)
{
    SwPaM* pPam = &rPam;
    auto [pStart, pEnd] = pPam->StartEnd(); // SwPosition*

    SwNodeIndex aPrevIdx( pStart->GetNode(), -1 );
    SwNodeOffset nOffsetTemp = pEnd->GetNodeIndex() - pStart->GetNodeIndex();
    const sal_Int32 nCntStt  = pStart->GetContentIndex();

    rDoc.SortText(rPam, *m_pOpt);

    pPam->DeleteMark();
    pPam->GetPoint()->Assign( aPrevIdx.GetNode(), SwNodeOffset(+1) );
    SwContentNode* pCNd = pPam->GetPointContentNode();
    sal_Int32 nLen = pCNd->Len();
    if( nLen > nCntStt )
        nLen = nCntStt;
    pPam->GetPoint()->SetContent( nLen );
    pPam->SetMark();

    pPam->GetPoint()->Adjust(nOffsetTemp);
    pCNd = pPam->GetPointContentNode();
    pPam->GetPoint()->SetContent( pCNd->Len() );

    SetValues( rPam );

    SetPaM( rPam );
    rPam.GetPoint()->Assign( m_nSaveEndNode, m_nSaveEndContent );
}

void SwUndoRedlineSort::RepeatImpl(::sw::RepeatContext & rContext)
{
    rContext.GetDoc().SortText( rContext.GetRepeatPaM(), *m_pOpt );
}

void SwUndoRedlineSort::SetSaveRange( const SwPaM& rRange )
{
    const SwPosition& rPos = *rRange.End();
    m_nSaveEndNode = rPos.GetNodeIndex();
    m_nSaveEndContent = rPos.GetContentIndex();
}

SwUndoAcceptRedline::SwUndoAcceptRedline( const SwPaM& rRange, sal_Int8 nDepth /* = 0 */ )
    : SwUndoRedline( SwUndoId::ACCEPT_REDLINE, rRange, nDepth )
{
}

void SwUndoAcceptRedline::RedoRedlineImpl(SwDoc & rDoc, SwPaM & rPam)
{
    rDoc.getIDocumentRedlineAccess().AcceptRedline(rPam, false, mnDepth);
}

void SwUndoAcceptRedline::RepeatImpl(::sw::RepeatContext & rContext)
{
    rContext.GetDoc().getIDocumentRedlineAccess().AcceptRedline(rContext.GetRepeatPaM(), true);
}

SwUndoRejectRedline::SwUndoRejectRedline( const SwPaM& rRange, sal_Int8 nDepth /* = 0 */, bool bHierarchical /*= false*/ )
    : SwUndoRedline( SwUndoId::REJECT_REDLINE, rRange, nDepth, bHierarchical )
{
}

void SwUndoRejectRedline::RedoRedlineImpl(SwDoc & rDoc, SwPaM & rPam)
{
    rDoc.getIDocumentRedlineAccess().RejectRedline(rPam, false, mnDepth);
}

void SwUndoRejectRedline::RepeatImpl(::sw::RepeatContext & rContext)
{
    rContext.GetDoc().getIDocumentRedlineAccess().RejectRedline(rContext.GetRepeatPaM(), true);
}

SwUndoCompDoc::SwUndoCompDoc( const SwPaM& rRg, bool bIns )
    : SwUndo( SwUndoId::COMPAREDOC, rRg.GetDoc() ), SwUndRng( rRg ),
    m_bInsert( bIns )
{
    SwDoc& rDoc = rRg.GetDoc();
    if( rDoc.getIDocumentRedlineAccess().IsRedlineOn() )
    {
        RedlineType eTyp = m_bInsert ? RedlineType::Insert : RedlineType::Delete;
        m_pRedlineData.reset( new SwRedlineData( eTyp, rDoc.getIDocumentRedlineAccess().GetRedlineAuthor() ) );
        SetRedlineFlags( rDoc.getIDocumentRedlineAccess().GetRedlineFlags() );
    }
}

SwUndoCompDoc::SwUndoCompDoc( const SwRangeRedline& rRedl )
    : SwUndo( SwUndoId::COMPAREDOC, rRedl.GetDoc() ), SwUndRng( rRedl ),
    // for MergeDoc the corresponding inverse is needed
    m_bInsert( RedlineType::Delete == rRedl.GetType() )
{
    SwDoc& rDoc = rRedl.GetDoc();
    if( rDoc.getIDocumentRedlineAccess().IsRedlineOn() )
    {
        m_pRedlineData.reset( new SwRedlineData( rRedl.GetRedlineData() ) );
        SetRedlineFlags( rDoc.getIDocumentRedlineAccess().GetRedlineFlags() );
    }

    m_pRedlineSaveDatas.reset( new SwRedlineSaveDatas );
    if( !FillSaveData( rRedl, *m_pRedlineSaveDatas, false ))
    {
        m_pRedlineSaveDatas.reset();
    }
}

SwUndoCompDoc::~SwUndoCompDoc()
{
    m_pRedlineData.reset();
    m_pUndoDelete.reset();
    m_pUndoDelete2.reset();
    m_pRedlineSaveDatas.reset();
}

void SwUndoCompDoc::UndoImpl(::sw::UndoRedoContext & rContext)
{
    SwDoc& rDoc = rContext.GetDoc();
    SwPaM& rPam(AddUndoRedoPaM(rContext));

    if( !m_bInsert )
    {
        // delete Redlines
        RedlineFlags eOld = rDoc.getIDocumentRedlineAccess().GetRedlineFlags();
        rDoc.getIDocumentRedlineAccess().SetRedlineFlags_intern(( eOld & ~RedlineFlags::Ignore) | RedlineFlags::On);

        rDoc.getIDocumentRedlineAccess().DeleteRedline(rPam, true, RedlineType::Any);

        rDoc.getIDocumentRedlineAccess().SetRedlineFlags_intern( eOld );

        // per definition Point is end (in SwUndRng!)
        SwContentNode* pCSttNd = rPam.GetMarkContentNode();
        SwContentNode* pCEndNd = rPam.GetPointContentNode();

        // if start- and end-content is zero, then the doc-compare moves
        // complete nodes into the current doc. And then the selection
        // must be from end to start, so the delete join into the right
        // direction.
        if( !m_nSttContent && !m_nEndContent )
            rPam.Exchange();

        bool bJoinText, bJoinPrev;
        sw_GetJoinFlags(rPam, bJoinText, bJoinPrev);

        m_pUndoDelete.reset(new SwUndoDelete(rPam, SwDeleteFlags::Default, false));

        if( bJoinText )
            sw_JoinText(rPam, bJoinPrev);

        if( pCSttNd && !pCEndNd)
        {
            // #112139# Do not step behind the end of content.
            SwNode & rTmp = rPam.GetPointNode();
            SwNode * pEnd = rDoc.GetNodes().DocumentSectionEndNode(&rTmp);

            if (&rTmp != pEnd)
            {
                rPam.SetMark();
                rPam.GetPoint()->Adjust(SwNodeOffset(1));
                m_pUndoDelete2.reset(new SwUndoDelete(rPam, SwDeleteFlags::Default, true));
            }
        }
        rPam.DeleteMark();
    }
    else
    {
        if( IDocumentRedlineAccess::IsRedlineOn( GetRedlineFlags() ))
        {
            rDoc.getIDocumentRedlineAccess().DeleteRedline(rPam, true, RedlineType::Any);

            if( m_pRedlineSaveDatas )
                SetSaveData(rDoc, *m_pRedlineSaveDatas);
        }
        SetPaM(rPam, true);
    }
}

void SwUndoCompDoc::RedoImpl(::sw::UndoRedoContext & rContext)
{
    SwDoc& rDoc = rContext.GetDoc();

    if( m_bInsert )
    {
        SwPaM& rPam(AddUndoRedoPaM(rContext));
        if( m_pRedlineData && IDocumentRedlineAccess::IsRedlineOn( GetRedlineFlags() ))
        {
            SwRangeRedline* pTmp = new SwRangeRedline(*m_pRedlineData, rPam);
            rDoc.getIDocumentRedlineAccess().GetRedlineTable().Insert( pTmp );
            pTmp->InvalidateRange(SwRangeRedline::Invalidation::Add);
        }
        else if( !( RedlineFlags::Ignore & GetRedlineFlags() ) &&
                !rDoc.getIDocumentRedlineAccess().GetRedlineTable().empty() )
        {
            rDoc.getIDocumentRedlineAccess().SplitRedline(rPam);
        }
        SetPaM(rPam, true);
    }
    else
    {
        if( m_pUndoDelete2 )
        {
            m_pUndoDelete2->UndoImpl(rContext);
            m_pUndoDelete2.reset();
        }
        m_pUndoDelete->UndoImpl(rContext);
        m_pUndoDelete.reset();

        // note: don't call SetPaM before executing Undo of members
        SwPaM& rPam(AddUndoRedoPaM(rContext));

        SwRangeRedline* pTmp = new SwRangeRedline(*m_pRedlineData, rPam);
        rDoc.getIDocumentRedlineAccess().GetRedlineTable().Insert( pTmp );
        pTmp->InvalidateRange(SwRangeRedline::Invalidation::Add);

        SetPaM(rPam, true);
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
