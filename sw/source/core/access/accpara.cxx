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
#include <numeric>
#include <txtfrm.hxx>
#include <flyfrm.hxx>
#include <mdiexp.hxx>
#include <ndtxt.hxx>
#include <pam.hxx>
#include <unotextrange.hxx>
#include <unocrsrhelper.hxx>
#include <crstate.hxx>
#include <accmap.hxx>
#include <fesh.hxx>
#include <viewopt.hxx>
#include <vcl/svapp.hxx>
#include <vcl/unohelp.hxx>
#include <vcl/window.hxx>
#include <sal/log.hxx>
#include <com/sun/star/accessibility/AccessibleRole.hpp>
#include <com/sun/star/accessibility/AccessibleScrollType.hpp>
#include <com/sun/star/accessibility/AccessibleStateType.hpp>
#include <com/sun/star/accessibility/AccessibleTextType.hpp>
#include <com/sun/star/accessibility/AccessibleEventId.hpp>
#include <com/sun/star/i18n/Boundary.hpp>
#include <com/sun/star/i18n/CharacterIteratorMode.hpp>
#include <com/sun/star/i18n/WordType.hpp>
#include <com/sun/star/i18n/XBreakIterator.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <com/sun/star/beans/UnknownPropertyException.hpp>
#include <breakit.hxx>
#include "accpara.hxx"
#include "accportions.hxx"
#include <sfx2/viewsh.hxx>
#include <sfx2/viewfrm.hxx>
#include <sfx2/dispatch.hxx>
#include <unocrsr.hxx>
#include <unoport.hxx>
#include <doc.hxx>
#include <IDocumentRedlineAccess.hxx>
#include "acchyperlink.hxx"
#include "acchypertextdata.hxx"
#include <unotools/accessiblerelationsethelper.hxx>
#include <com/sun/star/accessibility/AccessibleRelationType.hpp>
#include <comphelper/accessibletexthelper.hxx>
#include <algorithm>
#include <docufld.hxx>
#include <txtfld.hxx>
#include <fmtfld.hxx>
#include <modcfg.hxx>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <swmodule.hxx>
#include <redline.hxx>
#include <com/sun/star/awt/FontWeight.hpp>
#include <com/sun/star/awt/FontStrikeout.hpp>
#include <com/sun/star/awt/FontSlant.hpp>
#include <wrong.hxx>
#include <editeng/brushitem.hxx>
#include <editeng/unoprnms.hxx>
#include <editeng/lrspitem.hxx>
#include <editeng/ulspitem.hxx>
#include <swatrset.hxx>
#include <unosett.hxx>
#include <unomap.hxx>
#include <unoprnms.hxx>
#include <com/sun/star/text/WritingMode2.hpp>
#include <viewimp.hxx>
#include "textmarkuphelper.hxx"
#include "parachangetrackinginfo.hxx"
#include <com/sun/star/text/TextMarkupType.hpp>
#include <cppuhelper/typeprovider.hxx>
#include <svx/colorwindow.hxx>
#include <o3tl/string_view.hxx>
#include <editeng/editids.hrc>

#include <reffld.hxx>
#include <flddat.hxx>
#include "../../uibase/inc/fldmgr.hxx"
#include <fldbas.hxx>      // SwField

using namespace ::com::sun::star;
using namespace ::com::sun::star::accessibility;

using beans::PropertyValue;
using beans::UnknownPropertyException;
using beans::PropertyState_DIRECT_VALUE;

namespace com::sun::star::text {
    class XText;
}

const SwTextFrame* SwAccessibleParagraph::GetTextFrame() const
{
    const SwFrame* pFrame = GetFrame();
    assert(!pFrame || pFrame->IsTextFrame());
    return static_cast<const SwTextFrame*>(pFrame);
}

OUString const & SwAccessibleParagraph::GetString()
{
    return GetPortionData().GetAccessibleString();
}

sal_Int32 SwAccessibleParagraph::GetCaretPos()
{
    // get the selection's point, and test whether it's in our node
    // #i27301# - consider adjusted method signature
    SwPaM* pCaret = GetCursor( false );  // caret is first PaM in PaM-ring
    if (!pCaret)
        // no cursor -> no caret
        return -1;

    const SwTextFrame* const pTextFrame = GetTextFrame();
    assert(pTextFrame);

    // check whether the point points into 'our' node
    SwPosition* pPoint = pCaret->GetPoint();

    if (!sw::FrameContainsNode(*pTextFrame, pPoint->GetNodeIndex()))
        // not in this paragraph
        return -1;

    sal_Int32 nRet = -1;

    // check whether it's also within 'our' part of the paragraph
    const TextFrameIndex nIndex = pTextFrame->MapModelToViewPos(*pPoint);
    if(!GetPortionData().IsValidCorePosition( nIndex ) ||
        (GetPortionData().IsZeroCorePositionData()
          && nIndex == TextFrameIndex(0)))
    {
        bool bFormat = pTextFrame->HasPara();
        if(bFormat)
        {
            ClearPortionData();
            UpdatePortionData();
        }
    }
    if( GetPortionData().IsValidCorePosition( nIndex ) )
    {
        // Yes, it's us!
        // consider that cursor/caret is in front of the list label
        if ( pCaret->IsInFrontOfLabel() )
        {
            nRet = 0;
        }
        else
        {
            nRet = GetPortionData().GetAccessiblePosition( nIndex );
        }

        OSL_ENSURE( nRet >= 0, "invalid cursor?" );
        OSL_ENSURE( nRet <= GetPortionData().GetAccessibleString().
                                      getLength(), "invalid cursor?" );
    }
    // else: in this paragraph, but in different frame

    return nRet;
}

// #i27301# - new parameter <_bForSelection>
SwPaM* SwAccessibleParagraph::GetCursor( const bool _bForSelection )
{
    // get the cursor shell; if we don't have any, we don't have a
    // cursor/selection either
    SwPaM* pCursor = nullptr;
    SwCursorShell* pCursorShell = GetCursorShell();
    // #i27301# - if cursor is retrieved for selection, the cursors for
    // a table selection has to be returned.
    if ( pCursorShell != nullptr &&
         ( _bForSelection || !pCursorShell->IsTableMode() ) )
    {
        SwFEShell *pFESh = dynamic_cast<SwFEShell*>(pCursorShell);
        if( !pFESh ||
            !(pFESh->IsFrameSelected() || pFESh->GetSelectedObjCount() > 0) )
        {
            // get the selection, and test whether it affects our text node
            pCursor = pCursorShell->GetCursor( false /* ??? */ );
        }
    }

    return pCursor;
}

bool SwAccessibleParagraph::IsHeading() const
{
    const SwTextFrame* const pFrame = GetTextFrame();
    const SwTextNode *pTextNd = pFrame->GetTextNodeForParaProps();
    return pTextNd->IsOutline();
}

void SwAccessibleParagraph::GetStates( sal_Int64& rStateSet )
{
    SwAccessibleContext::GetStates( rStateSet );

    // MULTILINE
    rStateSet |= AccessibleStateType::MULTI_LINE;

    if (GetCursorShell())
    {
        // MULTISELECTABLE
        rStateSet |= AccessibleStateType::MULTI_SELECTABLE;
        // FOCUSABLE
        rStateSet |= AccessibleStateType::FOCUSABLE;
    }

    // FOCUSED (simulates node index of cursor)
    SwPaM* pCaret = GetCursor( false ); // #i27301# - consider adjusted method signature
    const SwTextFrame* const pFrame = GetTextFrame();
    assert(pFrame);
    if (pCaret != nullptr &&
        sw::FrameContainsNode(*pFrame, pCaret->GetPoint()->GetNodeIndex()) &&
        HasCursor())
    {
        vcl::Window *pWin = GetWindow();
        if( pWin && pWin->HasFocus() )
            rStateSet |= AccessibleStateType::FOCUSED;
        ::rtl::Reference < SwAccessibleContext > xThis( this );
        GetMap()->SetCursorContext( xThis );
    }
}

void SwAccessibleParagraph::InvalidateContent_( bool bVisibleDataFired )
{
    OUString sOldText( GetString() );

    ClearPortionData();

    const OUString sText = GetString();

    if( sText != sOldText )
    {
        // The text is changed
        // determine exact changes between sOldText and sText
        uno::Any aOldValue;
        uno::Any aNewValue;
        (void)comphelper::OCommonAccessibleText::implInitTextChangedEvent(sOldText, sText,
                                                                          aOldValue, aNewValue);

        FireAccessibleEvent(AccessibleEventId::TEXT_CHANGED, aOldValue, aNewValue);
        rtl::Reference<SwAccessibleContext> xParent = getAccessibleParentImpl();
        if (xParent.is() && xParent->getAccessibleRole() == AccessibleRole::TABLE_CELL)
            xParent->FireAccessibleEvent(AccessibleEventId::VALUE_CHANGED, uno::Any(), uno::Any());
    }
    else if( !bVisibleDataFired )
    {
        FireVisibleDataEvent();
    }

    bool bNewIsBlockQuote = IsBlockQuote();
    bool bNewIsHeading = IsHeading();
    //Get the real heading level, Heading1 ~ Heading10
    m_nHeadingLevel = GetRealHeadingLevel();
    bool bOldIsBlockQuote;
    bool bOldIsHeading;
    {
        std::scoped_lock aGuard( m_Mutex );
        bOldIsBlockQuote = m_bIsBlockQuote;
        bOldIsHeading = m_bIsHeading;
        m_bIsBlockQuote = bNewIsBlockQuote;
        if( m_bIsHeading != bNewIsHeading )
            m_bIsHeading = bNewIsHeading;
    }

    if (bNewIsBlockQuote != bOldIsBlockQuote || bNewIsHeading != bOldIsHeading)
    {
        // The role has changed
        FireAccessibleEvent(AccessibleEventId::ROLE_CHANGED, uno::Any(), uno::Any());
    }
}

void SwAccessibleParagraph::InvalidateCursorPos_()
{
    // The text is changed
    sal_Int32 nNew = GetCaretPos();
    sal_Int32 nOld;
    {
        std::scoped_lock aGuard( m_Mutex );
        nOld = m_nOldCaretPos;
        m_nOldCaretPos = nNew;
    }
    if( -1 != nNew )
    {
        // remember that object as the one that has the caret. This is
        // necessary to notify that object if the cursor leaves it.
        GetMap()->SetCursorContext(this);
    }

    if( nOld == nNew )
        return;

    // The cursor's node position is simulated by the focus!
    vcl::Window* pWin = GetWindow();
    if( pWin && pWin->HasFocus() && -1 == nOld )
        FireStateChangedEvent( AccessibleStateType::FOCUSED, true );

    FireAccessibleEvent(AccessibleEventId::CARET_CHANGED, uno::Any(nOld), uno::Any(nNew));

    if( pWin && pWin->HasFocus() && -1 == nNew )
        FireStateChangedEvent( AccessibleStateType::FOCUSED, false );
    //To send TEXT_SELECTION_CHANGED event
    sal_Int32 nStart=0;
    sal_Int32 nEnd  =0;
    bool bCurSelection = GetSelection(nStart,nEnd);
    if(m_bLastHasSelection || bCurSelection )
    {
        FireAccessibleEvent(AccessibleEventId::TEXT_SELECTION_CHANGED, uno::Any(), uno::Any());
    }
    m_bLastHasSelection =bCurSelection;

}

void SwAccessibleParagraph::InvalidateFocus_()
{
    vcl::Window *pWin = GetWindow();
    if( pWin )
    {
        sal_Int32 nPos;
        {
            std::scoped_lock aGuard( m_Mutex );
            nPos = m_nOldCaretPos;
        }
        OSL_ENSURE( nPos != -1, "focus object should be selected" );

        FireStateChangedEvent( AccessibleStateType::FOCUSED,
                               pWin->HasFocus() && nPos != -1 );
    }
}

SwAccessibleParagraph::SwAccessibleParagraph(
        std::shared_ptr<SwAccessibleMap> const& pInitMap,
        const SwTextFrame& rTextFrame )
    : SwAccessibleParagraph_BASE(pInitMap, AccessibleRole::PARAGRAPH, &rTextFrame)
    , m_nOldCaretPos( -1 )
    , m_bIsBlockQuote(false)
    , m_bIsHeading( false )
    //Get the real heading level, Heading1 ~ Heading10
    , m_nHeadingLevel (-1)
    , m_aSelectionHelper( *this )
    , mpParaChangeTrackInfo( new SwParaChangeTrackingInfo( rTextFrame ) ) // #i108125#
    , m_bLastHasSelection(false)  //To add TEXT_SELECTION_CHANGED event
{
    StartListening(const_cast<SwTextFrame&>(rTextFrame));
    m_bIsBlockQuote = IsBlockQuote();
    m_bIsHeading = IsHeading();
    //Get the real heading level, Heading1 ~ Heading10
    m_nHeadingLevel = GetRealHeadingLevel();
    SetName( OUString() ); // set an empty accessibility name for paragraphs
}

SwAccessibleParagraph::~SwAccessibleParagraph()
{
    SolarMutexGuard aGuard;

    m_pPortionData.reset();
    m_pHyperTextData.reset();
    mpParaChangeTrackInfo.reset(); // #i108125#
    EndListeningAll();
}

bool SwAccessibleParagraph::HasCursor()
{
    std::scoped_lock aGuard( m_Mutex );
    return m_nOldCaretPos != -1;
}

void SwAccessibleParagraph::UpdatePortionData()
{
    // obtain the text frame
    const SwTextFrame* pFrame = GetTextFrame();
    assert(pFrame && "The text frame has vanished!");
    // build new portion data
    m_pPortionData.reset(
        new SwAccessiblePortionData(*pFrame, GetMap()->GetShell().GetViewOptions()));
    pFrame->VisitPortions(*m_pPortionData);
}

void SwAccessibleParagraph::ClearPortionData()
{
    m_pPortionData.reset();
    m_pHyperTextData.reset();
}

void SwAccessibleParagraph::ExecuteAtViewShell( sal_uInt16 nSlot )
{
    OSL_ENSURE( GetMap() != nullptr, "no map?" );
    SwViewShell& rViewShell = GetMap()->GetShell();

    SfxViewShell* pSfxShell = rViewShell.GetSfxViewShell();

    OSL_ENSURE( pSfxShell != nullptr, "SfxViewShell shell expected!" );
    if( !pSfxShell )
        return;

    SfxViewFrame& rFrame = pSfxShell->GetViewFrame();
    SfxDispatcher *pDispatcher = rFrame.GetDispatcher();
    OSL_ENSURE( pDispatcher != nullptr, "Dispatcher expected!" );
    if( !pDispatcher )
        return;

    pDispatcher->Execute( nSlot );
}

rtl::Reference<SwXTextPortion> SwAccessibleParagraph::CreateUnoPortion(
    sal_Int32 nStartIndex,
    sal_Int32 nEndIndex )
{
    OSL_ENSURE( (IsValidChar(nStartIndex, GetString().getLength()) &&
                 (nEndIndex == -1)) ||
                IsValidRange(nStartIndex, nEndIndex, GetString().getLength()),
                "please check parameters before calling this method" );

    const TextFrameIndex nStart = GetPortionData().GetCoreViewPosition(nStartIndex);
    const TextFrameIndex nEnd = (nEndIndex == -1)
            ? (nStart + TextFrameIndex(1))
            : GetPortionData().GetCoreViewPosition(nEndIndex);

    // create UNO cursor
    const SwTextFrame* const pFrame = GetTextFrame();
    SwPosition aStartPos(pFrame->MapViewToModelPos(nStart));
    auto pUnoCursor(const_cast<SwDoc&>(pFrame->GetDoc()).CreateUnoCursor(aStartPos));
    pUnoCursor->SetMark();
    *pUnoCursor->GetMark() = pFrame->MapViewToModelPos(nEnd);

    // create a (dummy) text portion to be returned
    uno::Reference<SwXText> aEmpty;
    return new SwXTextPortion ( pUnoCursor.get(), aEmpty, PORTION_TEXT);
}

// range checking for parameter

bool SwAccessibleParagraph::IsValidChar(
    sal_Int32 nPos, sal_Int32 nLength)
{
    return (nPos >= 0) && (nPos < nLength);
}

bool SwAccessibleParagraph::IsValidPosition(
    sal_Int32 nPos, sal_Int32 nLength)
{
    return (nPos >= 0) && (nPos <= nLength);
}

bool SwAccessibleParagraph::IsValidRange(
    sal_Int32 nBegin, sal_Int32 nEnd, sal_Int32 nLength)
{
    return IsValidPosition(nBegin, nLength) && IsValidPosition(nEnd, nLength);
}

//the function is to check whether the position is in a redline range.
const SwRangeRedline* SwAccessibleParagraph::GetRedlineAtIndex()
{
    const SwRangeRedline* pRedline = nullptr;
    SwPaM* pCrSr = GetCursor( true );
    if ( pCrSr )
    {
        SwPosition* pStart = pCrSr->Start();
        pRedline = pStart->GetDoc().getIDocumentRedlineAccess().GetRedline(*pStart, nullptr);
    }

    return pRedline;
}

// text boundaries

bool SwAccessibleParagraph::GetCharBoundary(
    i18n::Boundary& rBound,
    std::u16string_view text,
    sal_Int32 nPos )
{
    if( GetPortionData().FillBoundaryIFDateField( rBound,  nPos) )
        return true;

    auto nPosEnd = nPos;
    o3tl::iterateCodePoints(text, &nPosEnd);

    rBound.startPos = nPos;
    rBound.endPos = nPosEnd;

    return true;
}

bool SwAccessibleParagraph::GetWordBoundary(
    i18n::Boundary& rBound,
    const OUString& rText,
    sal_Int32 nPos )
{
    // now ask the Break-Iterator for the word
    assert(g_pBreakIt && g_pBreakIt->GetBreakIter().is());

    // get locale for this position
    const SwTextFrame* const pFrame = GetTextFrame();
    const TextFrameIndex nCorePos = GetPortionData().GetCoreViewPosition(nPos);
    lang::Locale aLocale = g_pBreakIt->GetLocale(pFrame->GetLangOfChar(nCorePos, 0, true));

    // which type of word are we interested in?
    // (DICTIONARY_WORD includes punctuation, ANY_WORD doesn't.)
    const sal_Int16 nWordType = i18n::WordType::ANY_WORD;

    // get word boundary, as the Break-Iterator sees fit.
    rBound = g_pBreakIt->GetBreakIter()->getWordBoundary(
        rText, nPos, aLocale, nWordType, true );

    return true;
}

bool SwAccessibleParagraph::GetSentenceBoundary(
    i18n::Boundary& rBound,
    const OUString& rText,
    sal_Int32 nPos )
{
    const sal_Unicode* pStr = rText.getStr();
    while( nPos < rText.getLength() && pStr[nPos] == u' ' )
        nPos++;

    GetPortionData().GetSentenceBoundary( rBound, nPos );
    return true;
}

bool SwAccessibleParagraph::GetLineBoundary(
    i18n::Boundary& rBound,
    std::u16string_view aText,
    sal_Int32 nPos )
{
    if( sal_Int32(aText.size()) == nPos )
        GetPortionData().GetLastLineBoundary( rBound );
    else
        GetPortionData().GetLineBoundary( rBound, nPos );
    return true;
}

bool SwAccessibleParagraph::GetParagraphBoundary(
    i18n::Boundary& rBound,
    std::u16string_view aText )
{
    rBound.startPos = 0;
    rBound.endPos = aText.size();
    return true;
}

bool SwAccessibleParagraph::GetAttributeBoundary(
    i18n::Boundary& rBound,
    sal_Int32 nPos )
{
    GetPortionData().GetAttributeBoundary( rBound, nPos );
    return true;
}

bool SwAccessibleParagraph::GetGlyphBoundary(
    i18n::Boundary& rBound,
    const OUString& rText,
    sal_Int32 nPos )
{
    // ask the Break-Iterator for the glyph by moving one cell
    // forward, and then one cell back
    assert(g_pBreakIt && g_pBreakIt->GetBreakIter().is());

    // get locale for this position
    const SwTextFrame* const pFrame = GetTextFrame();
    const TextFrameIndex nCorePos = GetPortionData().GetCoreViewPosition(nPos);
    lang::Locale aLocale = g_pBreakIt->GetLocale(pFrame->GetLangOfChar(nCorePos, 0, true));

    // get word boundary, as the Break-Iterator sees fit.
    const sal_Int16 nIterMode = i18n::CharacterIteratorMode::SKIPCELL;
    sal_Int32 nDone = 0;
    rBound.endPos = g_pBreakIt->GetBreakIter()->nextCharacters(
         rText, nPos, aLocale, nIterMode, 1, nDone );
    rBound.startPos = g_pBreakIt->GetBreakIter()->previousCharacters(
         rText, rBound.endPos, aLocale, nIterMode, 1, nDone );
    bool bRet = ((rBound.startPos <= nPos) && (nPos <= rBound.endPos));
    OSL_ENSURE( rBound.startPos <= nPos, "start pos too high" );
    OSL_ENSURE( rBound.endPos >= nPos, "end pos too low" );

    return bRet;
}

bool SwAccessibleParagraph::GetTextBoundary(
    i18n::Boundary& rBound,
    const OUString& rText,
    sal_Int32 nPos,
    sal_Int16 nTextType )
{
    // error checking
    if( !( AccessibleTextType::LINE == nTextType
                ? IsValidPosition( nPos, rText.getLength() )
                : IsValidChar( nPos, rText.getLength() ) ) )
        throw lang::IndexOutOfBoundsException();

    switch( nTextType )
    {
        case AccessibleTextType::WORD:
            return GetWordBoundary(rBound, rText, nPos);
        case AccessibleTextType::SENTENCE:
            return GetSentenceBoundary(rBound, rText, nPos);
        case AccessibleTextType::PARAGRAPH:
            return GetParagraphBoundary(rBound, rText);
        case AccessibleTextType::CHARACTER:
            return GetCharBoundary(rBound, rText, nPos);
        case AccessibleTextType::LINE:
            //Solve the problem of returning wrong LINE and PARAGRAPH
            if((nPos == rText.getLength()) && nPos > 0)
                return GetLineBoundary(rBound, rText, nPos - 1);
            else
                return GetLineBoundary(rBound, rText, nPos);
            break;
        case AccessibleTextType::ATTRIBUTE_RUN:
            return GetAttributeBoundary(rBound, nPos);
        case AccessibleTextType::GLYPH:
            return GetGlyphBoundary(rBound, rText, nPos);
        default:
            throw lang::IllegalArgumentException( );
    }
}

OUString SAL_CALL SwAccessibleParagraph::getAccessibleDescription()
{
    SolarMutexGuard aGuard;
    ThrowIfDisposed();

    return OUString();
}

lang::Locale SAL_CALL SwAccessibleParagraph::getLocale()
{
    SolarMutexGuard aGuard;

    const SwTextFrame *pTextFrame = GetFrame()->DynCastTextFrame();
    if( !pTextFrame )
    {
        throw uno::RuntimeException(u"no SwTextFrame"_ustr, getXWeak());
    }

    lang::Locale aLoc(g_pBreakIt->GetLocale(pTextFrame->GetLangOfChar(TextFrameIndex(0), 0, true)));

    return aLoc;
}

// #i27138# - paragraphs are in relation CONTENT_FLOWS_FROM and/or CONTENT_FLOWS_TO
uno::Reference<XAccessibleRelationSet> SAL_CALL SwAccessibleParagraph::getAccessibleRelationSet()
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    rtl::Reference<utl::AccessibleRelationSetHelper> pHelper = new utl::AccessibleRelationSetHelper();

    const SwTextFrame* pTextFrame = GetTextFrame();
    if (!pTextFrame)
        return pHelper;

    const SwContentFrame* pPrevContentFrame( pTextFrame->FindPrevCnt() );
    if ( pPrevContentFrame )
    {
        uno::Sequence<uno::Reference<XAccessible>> aSequence { GetMap()->GetContext(pPrevContentFrame) };
        AccessibleRelation aAccRel(AccessibleRelationType_CONTENT_FLOWS_FROM, aSequence);
        pHelper->AddRelation( aAccRel );
    }

    const SwContentFrame* pNextContentFrame( pTextFrame->FindNextCnt( true ) );
    if ( pNextContentFrame )
    {
        uno::Sequence<uno::Reference<XAccessible>> aSequence { GetMap()->GetContext(pNextContentFrame) };
        AccessibleRelation aAccRel(AccessibleRelationType_CONTENT_FLOWS_TO, aSequence);
        pHelper->AddRelation( aAccRel );
    }

    return pHelper;
}

void SAL_CALL SwAccessibleParagraph::grabFocus()
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    // get cursor shell
    SwCursorShell *pCursorSh = GetCursorShell();
    SwPaM *pCursor = GetCursor( false ); // #i27301# - consider new method signature
    const SwTextFrame* pTextFrame = GetTextFrame();

    if (pCursorSh != nullptr &&
        ( pCursor == nullptr ||
          !sw::FrameContainsNode(*pTextFrame, pCursor->GetPoint()->GetNodeIndex()) ||
          !pTextFrame->IsInside(pTextFrame->MapModelToViewPos(*pCursor->GetPoint()))))
    {
        // create pam for selection
        SwPosition const aStartPos(pTextFrame->MapViewToModelPos(pTextFrame->GetOffset()));
        SwPaM aPaM( aStartPos );

        // set PaM at cursor shell
        Select( aPaM );

    }

    // ->#i13955#
    vcl::Window * pWindow = GetWindow();

    if (pWindow != nullptr)
        pWindow->GrabFocus();
    // <-#i13955#
}

// #i71385#
static bool lcl_GetBackgroundColor( Color & rColor,
                             const SwFrame* pFrame,
                             SwCursorShell* pCursorSh )
{
    const SvxBrushItem* pBackgroundBrush = nullptr;
    std::optional<Color> xSectionTOXColor;
    SwRect aDummyRect;
    drawinglayer::attribute::SdrAllFillAttributesHelperPtr aFillAttributes;

    if ( pFrame &&
         pFrame->GetBackgroundBrush( aFillAttributes, pBackgroundBrush, xSectionTOXColor, aDummyRect, false, /*bConsiderTextBox=*/false ) )
    {
        if ( xSectionTOXColor )
        {
            rColor = *xSectionTOXColor;
            return true;
        }
        else
        {
            rColor =  pBackgroundBrush->GetColor();
            return true;
        }
    }
    else if ( pCursorSh )
    {
        rColor = pCursorSh->Imp()->GetRetoucheColor();
        return true;
    }

    return false;
}

sal_Int32 SAL_CALL SwAccessibleParagraph::getForeground()
{
    SolarMutexGuard g;

    Color aBackgroundCol;

    if ( lcl_GetBackgroundColor( aBackgroundCol, GetFrame(), GetCursorShell() ) )
    {
        if ( aBackgroundCol.IsDark() )
        {
            return sal_Int32(COL_WHITE);
        }
        else
        {
            return sal_Int32(COL_BLACK);
        }
    }

    return SwAccessibleContext::getForeground();
}

sal_Int32 SAL_CALL SwAccessibleParagraph::getBackground()
{
    SolarMutexGuard g;

    Color aBackgroundCol;

    if ( lcl_GetBackgroundColor( aBackgroundCol, GetFrame(), GetCursorShell() ) )
    {
        return sal_Int32(aBackgroundCol);
    }

    return SwAccessibleContext::getBackground();
}

static uno::Sequence< OUString > const & getAttributeNames()
{
    static uno::Sequence< OUString > const aNames
    {
        // Add the font name to attribute list
        // sorted list of strings
        UNO_NAME_CHAR_BACK_COLOR,
        UNO_NAME_CHAR_COLOR,
        UNO_NAME_CHAR_CONTOURED,
        UNO_NAME_CHAR_EMPHASIS,
        UNO_NAME_CHAR_ESCAPEMENT,
        UNO_NAME_CHAR_FONT_NAME,
        UNO_NAME_CHAR_HEIGHT,
        UNO_NAME_CHAR_POSTURE,
        UNO_NAME_CHAR_SHADOWED,
        UNO_NAME_CHAR_STRIKEOUT,
        UNO_NAME_CHAR_UNDERLINE,
        UNO_NAME_CHAR_UNDERLINE_COLOR,
        UNO_NAME_CHAR_WEIGHT,
    };
    return aNames;
}

static uno::Sequence< OUString > const & getSupplementalAttributeNames()
{
    static uno::Sequence< OUString > const aNames
    {
        // sorted list of strings
        UNO_NAME_NUMBERING_LEVEL,
        UNO_NAME_NUMBERING,
        UNO_NAME_NUMBERING_RULES,
        UNO_NAME_PARA_ADJUST,
        UNO_NAME_PARA_BOTTOM_MARGIN,
        UNO_NAME_PARA_FIRST_LINE_INDENT,
        UNO_NAME_PARA_LEFT_MARGIN,
        UNO_NAME_PARA_LINE_SPACING,
        UNO_NAME_PARA_RIGHT_MARGIN,
        UNO_NAME_TABSTOPS,
    };
    return aNames;
}

// XAccessibleText

sal_Int32 SwAccessibleParagraph::getCaretPosition()
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    sal_Int32 nRet = GetCaretPos();
    {
        std::scoped_lock aOldCaretPosGuard( m_Mutex );
        OSL_ENSURE( nRet == m_nOldCaretPos, "caret pos out of sync" );
        m_nOldCaretPos = nRet;
    }
    if( -1 != nRet )
    {
        GetMap()->SetCursorContext(this);
    }

    return nRet;
}

sal_Bool SAL_CALL SwAccessibleParagraph::setCaretPosition( sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    // parameter checking
    sal_Int32 nLength = GetString().getLength();
    if ( ! IsValidPosition( nIndex, nLength ) )
    {
        throw lang::IndexOutOfBoundsException();
    }

    bool bRet = false;

    // get cursor shell
    SwCursorShell* pCursorShell = GetCursorShell();
    if( pCursorShell != nullptr )
    {
        // create pam for selection
        const SwTextFrame* const pFrame = GetTextFrame();
        TextFrameIndex const nFrameIndex(GetPortionData().GetCoreViewPosition(nIndex));
        SwPosition aStartPos(pFrame->MapViewToModelPos(nFrameIndex));
        SwPaM aPaM( aStartPos );

        // set PaM at cursor shell
        bRet = Select( aPaM );
    }

    return bRet;
}

sal_Unicode SwAccessibleParagraph::getCharacter( sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    OUString sText( GetString() );

    // return character (if valid)
    if( !IsValidChar(nIndex, sText.getLength() ) )
        throw lang::IndexOutOfBoundsException();

    return sText[nIndex];
}

css::uno::Sequence< css::style::TabStop > SwAccessibleParagraph::GetCurrentTabStop( sal_Int32 nIndex  )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    /*  #i12332# The position after the string needs special treatment.
        IsValidChar -> IsValidPosition
    */
    if( ! (IsValidPosition( nIndex, GetString().getLength() ) ) )
        throw lang::IndexOutOfBoundsException();

    /*  #i12332#  */
    bool bBehindText = false;
    if ( nIndex == GetString().getLength() )
        bBehindText = true;

    // get model position & prepare GetCharRect() arguments
    SwCursorMoveState aMoveState;
    aMoveState.m_bRealHeight = true;
    aMoveState.m_bRealWidth = true;
    SwSpecialPos aSpecialPos;
    const SwTextFrame* const pFrame = GetTextFrame();

    /*  #i12332# FillSpecialPos does not accept nIndex ==
         GetString().getLength(). In that case nPos is set to the
         length of the string in the core. This way GetCharRect
         returns the rectangle for a cursor at the end of the
         paragraph. */
    const TextFrameIndex nPos = bBehindText
        ? TextFrameIndex(pFrame->GetText().getLength())
        : GetPortionData().FillSpecialPos(nIndex, aSpecialPos, aMoveState.m_pSpecialPos );

    // call GetCharRect
    SwRect aCoreRect;
    SwPosition aPosition(pFrame->MapViewToModelPos(nPos));
    GetFrame()->GetCharRect( aCoreRect, aPosition, &aMoveState );

    // already get the caret position
    css::uno::Sequence< css::style::TabStop > tabs;
    const sal_Int32 nStrLen = pFrame->GetText().getLength();
    if( nStrLen > 0 )
    {
        SwFrame* pTFrame = const_cast<SwFrame*>(GetFrame());
        tabs = pTFrame->GetTabStopInfo(aCoreRect.Left());
    }

    if( tabs.hasElements() )
    {
        // translate core coordinates into accessibility coordinates
        vcl::Window *pWin = GetWindow();
        if (!pWin)
        {
            throw uno::RuntimeException(u"no Window"_ustr, getXWeak());
        }

        SwRect aTmpRect(0, 0, tabs[0].Position, 0);

        tools::Rectangle aScreenRect( GetMap()->CoreToPixel( aTmpRect ));
        SwRect aFrameLogBounds( GetBounds( *(GetMap()) ) ); // twip rel to doc root

        Point aFramePixPos( GetMap()->CoreToPixel( aFrameLogBounds ).TopLeft() );
        aScreenRect.Move( -aFramePixPos.X(), -aFramePixPos.Y() );

        tabs.getArray()[0].Position = aScreenRect.GetWidth();
    }

    return tabs;
}

namespace {

struct IndexCompare
{
    const PropertyValue* pValues;
    explicit IndexCompare( const PropertyValue* pVals ) : pValues(pVals) {}
    bool operator() ( sal_Int32 a, sal_Int32 b ) const
    {
        return (pValues[a].Name < pValues[b].Name);
    }
};

}

OUString SwAccessibleParagraph::GetFieldTypeNameAtIndex(sal_Int32 nIndex)
{
    sal_Int32 nFieldIndex = GetPortionData().GetFieldIndex(nIndex);
    if (nFieldIndex < 0)
        return OUString();

    SwFieldMgr aMgr;
    SwTextField* pTextField = nullptr;
    OUString strTypeName;

    const SwTextFrame* const pFrame = GetTextFrame();
    sw::MergedAttrIter iter(*pFrame);
    while (SwTextAttr const*const pHt = iter.NextAttr())
    {
        if ((pHt->Which() == RES_TXTATR_FIELD
               || pHt->Which() == RES_TXTATR_ANNOTATION
               || pHt->Which() == RES_TXTATR_INPUTFIELD)
             && (nFieldIndex-- == 0))
        {
            pTextField = const_cast<SwTextField*>(
                        static_txtattr_cast<SwTextField const*>(pHt));
            break;
        }
        else if (pHt->Which() == RES_TXTATR_REFMARK
                 && (nFieldIndex-- == 0))
        {
            strTypeName = "set reference";
        }
    }

    if (!pTextField)
        return strTypeName;

    const SwField* pField = pTextField->GetFormatField().GetField();
    if (!pField)
        return strTypeName;

    strTypeName = SwFieldType::GetTypeStr(pField->GetTypeId());
    const SwFieldIds nWhich = pField->GetTyp()->Which();
    OUString sEntry;
    sal_uInt32 subType = 0;
    switch (nWhich)
    {
    case SwFieldIds::DocStat:
        subType = static_cast<sal_uInt16>(static_cast<const SwDocStatField*>(pField)->GetSubType());
        break;
    case SwFieldIds::GetRef:
        {
            const SwGetRefField* pRefField = static_cast<const SwGetRefField*>(pField);
            switch( pRefField->GetSubType() )
            {
            case ReferencesSubtype::Bookmark:
                {
                    if (  pRefField->IsRefToHeadingCrossRefBookmark() )
                        sEntry = "Headings";
                    else if (  pRefField->IsRefToNumItemCrossRefBookmark() )
                        sEntry = "Numbered Paragraphs";
                    else
                        sEntry = "Bookmarks";
                }
                break;
            case ReferencesSubtype::Footnote:
                sEntry = "Footnotes";
                break;
            case ReferencesSubtype::Endnote:
                sEntry = "Endnotes";
                break;
            case ReferencesSubtype::SetRefAttr:
                sEntry = "Insert Reference";
                break;
            case ReferencesSubtype::SequenceField:
                sEntry = pRefField->GetSetRefName().toString();
                break;
            case ReferencesSubtype::Style:
                sEntry = "StyleRef";
                break;
            default: break; // ReferencesSubtype::Outline not handled?
            }
            //Get format string
            strTypeName = sEntry;
            // <pField->GetFormat() >= 0> is always true as <pField->GetFormat()> is unsigned
//                    if (pField->GetFormat() >= 0)
            {
                sEntry = aMgr.GetFormatStr( *pField );
                if (sEntry.getLength() > 0)
                {
                    strTypeName += "-" + sEntry;
                }
            }
        }
        break;
    case SwFieldIds::DateTime:
        subType = static_cast<sal_uInt16>(static_cast<const SwDateTimeField*>(pField)->GetSubType());
        break;
    case SwFieldIds::JumpEdit:
        {
            const SwJumpEditFormat nFormat = static_cast<const SwJumpEditField*>(pField)->GetFormat();
            const sal_uInt16 nSize = aMgr.GetFormatCount(pField->GetTypeId(), false);
            if (static_cast<sal_uInt32>(nFormat) < nSize)
            {
                sEntry = aMgr.GetFormatStr(pField->GetTypeId(), static_cast<sal_uInt32>(nFormat));
                if (sEntry.getLength() > 0)
                {
                    strTypeName += "-" + sEntry;
                }
            }
        }
        break;
    case SwFieldIds::ExtUser:
        subType = static_cast<sal_uInt16>(static_cast<const SwExtUserField*>(pField)->GetSubType());
        break;
    case SwFieldIds::HiddenText:
    case SwFieldIds::SetExp:
        {
            sEntry = pField->GetTyp()->GetName().toString();
            if (sEntry.getLength() > 0)
            {
                strTypeName += "-" + sEntry;
            }
        }
        break;
    case SwFieldIds::DocInfo:
        {
            const SwDocInfoField* pDocInfoField = static_cast<const SwDocInfoField*>(pField);
            subType = static_cast<sal_uInt16>(pDocInfoField->GetSubType() & SwDocInfoSubType::LowerMask);
        }
        break;
    case SwFieldIds::RefPageSet:
        {
            const SwRefPageSetField* pRPld = static_cast<const SwRefPageSetField*>(pField);
            bool bOn = pRPld->IsOn();
            strTypeName += "-";
            if (bOn)
                strTypeName += "on";
            else
                strTypeName += "off";
        }
        break;
    case SwFieldIds::Author:
        {
            const SwAuthorField* pAuthorField = static_cast<const SwAuthorField*>(pField);
            strTypeName += "-" + aMgr.GetFormatStr(pField->GetTypeId(), static_cast<sal_uInt32>(pAuthorField->GetFormat() & SwAuthorFormat::Mask));
        }
        break;
    default: break;
    }

    if (subType > 0 || nWhich == SwFieldIds::DocInfo || nWhich == SwFieldIds::ExtUser || nWhich == SwFieldIds::DocStat)
    {
        std::vector<OUString> aLst;
        aMgr.GetSubTypes(pField->GetTypeId(), aLst);
        if (subType < aLst.size())
            sEntry = aLst[subType];
        if (sEntry.getLength() > 0)
        {
            if (nWhich == SwFieldIds::DocInfo)
            {
                strTypeName = sEntry;
                sal_uInt16 nSize = aMgr.GetFormatCount(pField->GetTypeId(), false);
                auto pDocInfoField = static_cast<const SwDocInfoField*>(pField);
                const sal_uInt16 nExSub = static_cast<sal_uInt16>(pDocInfoField->GetSubType() & SwDocInfoSubType::UpperMask);
                if (nSize > 0 && nExSub > 0)
                {
                    //Get extra subtype string
                    strTypeName += "-";
                    sEntry = aMgr.GetFormatStr(pField->GetTypeId(), nExSub/0x0100-1);
                    strTypeName += sEntry;
                }
            }
            else
            {
                strTypeName += "-" + sEntry;
            }
        }
    }
    return strTypeName;
}

// #i63870# - re-implement method on behalf of methods
// <_getDefaultAttributesImpl(..)> and <_getRunAttributesImpl(..)>
uno::Sequence<PropertyValue> SwAccessibleParagraph::getCharacterAttributes(
    sal_Int32 nIndex,
    const uno::Sequence< OUString >& aRequestedAttributes )
{

    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    const OUString& rText = GetString();

    if (!IsValidPosition(nIndex, rText.getLength()))
        throw lang::IndexOutOfBoundsException();

    bool bSupplementalMode = false;
    uno::Sequence< OUString > aNames = aRequestedAttributes;
    if (!aNames.hasElements())
    {
        bSupplementalMode = true;
        aNames = getAttributeNames();
    }
    // retrieve default character attributes
    tAccParaPropValMap aDefAttrSeq;
    _getDefaultAttributesImpl( aNames, aDefAttrSeq, true );

    // retrieved run character attributes
    tAccParaPropValMap aRunAttrSeq;
    _getRunAttributesImpl( nIndex, aNames, aRunAttrSeq );

    // this allows to request one or more supplemental attributes, only
    bSupplementalMode = bSupplementalMode || aDefAttrSeq.empty() || aRunAttrSeq.empty();

    // merge default and run attributes
    std::vector< PropertyValue > aValues( aDefAttrSeq.size() );
    sal_Int32 i = 0;
    for ( const auto& rDefEntry : aDefAttrSeq )
    {
        tAccParaPropValMap::const_iterator aRunIter =
                                        aRunAttrSeq.find( rDefEntry.first );
        if ( aRunIter != aRunAttrSeq.end() )
        {
            aValues[i] = aRunIter->second;
        }
        else
        {
            aValues[i] = rDefEntry.second;
        }
        ++i;
    }
    if( bSupplementalMode )
    {
        uno::Sequence< OUString > aSupplementalNames = aRequestedAttributes;
        if (!aSupplementalNames.hasElements())
            aSupplementalNames = getSupplementalAttributeNames();

        tAccParaPropValMap aSupplementalAttrSeq;
        _getSupplementalAttributesImpl( aSupplementalNames, aSupplementalAttrSeq );

        aValues.resize( aValues.size() + aSupplementalAttrSeq.size() );

        for ( const auto& rSupplementalEntry : aSupplementalAttrSeq )
        {
            aValues[i] = rSupplementalEntry.second;
            ++i;
        }

        _correctValues( nIndex, aValues );

        aValues.emplace_back();

        OUString strTypeName = GetFieldTypeNameAtIndex(nIndex);
        if (!strTypeName.isEmpty())
        {
            aValues.emplace_back();
            PropertyValue& rValueFT = aValues.back();
            rValueFT.Name = "FieldType";
            rValueFT.Value <<= strTypeName.toAsciiLowerCase();
            rValueFT.Handle = -1;
            rValueFT.State = PropertyState_DIRECT_VALUE;
        }

        //sort property values
        // build sorted index array
        sal_Int32 nLength = aValues.size();
        std::vector<sal_Int32> aIndices;
        aIndices.reserve(nLength);
        for (i = 0; i < nLength; ++i)
            aIndices.push_back(i);
        std::sort(aIndices.begin(), aIndices.end(), IndexCompare(aValues.data()));
        // create sorted sequences according to index array
        uno::Sequence<PropertyValue> aNewValues( nLength );
        PropertyValue* pNewValues = aNewValues.getArray();
        for (i = 0; i < nLength; ++i)
        {
            pNewValues[i] = aValues[aIndices[i]];
        }
        return aNewValues;
    }

    return comphelper::containerToSequence(aValues);
}

static void SetPutRecursive(SfxItemSet &targetSet, const SfxItemSet &sourceSet)
{
    const SfxItemSet *const pParentSet = sourceSet.GetParent();
    if (pParentSet)
        SetPutRecursive(targetSet, *pParentSet);
    targetSet.Put(sourceSet);
}

// #i63870#
void SwAccessibleParagraph::_getDefaultAttributesImpl(
        const uno::Sequence< OUString >& aRequestedAttributes,
        tAccParaPropValMap& rDefAttrSeq,
        const bool bOnlyCharAttrs )
{
    // retrieve default attributes
    const SwTextFrame* const pFrame = GetTextFrame();
    const SwTextNode *const pTextNode(pFrame->GetTextNodeForParaProps());
    std::optional<SfxItemSet> pSet;
    if ( !bOnlyCharAttrs )
    {
        pSet.emplace( const_cast<SwAttrPool&>(pTextNode->GetDoc().GetAttrPool()),
                               svl::Items<RES_CHRATR_BEGIN, RES_CHRATR_END - 1,
                               RES_PARATR_BEGIN, RES_PARATR_END - 1,
                               RES_FRMATR_BEGIN, RES_FRMATR_END - 1> );
    }
    else
    {
        pSet.emplace( const_cast<SwAttrPool&>(pTextNode->GetDoc().GetAttrPool()),
                               svl::Items<RES_CHRATR_BEGIN, RES_CHRATR_END - 1> );
    }
    // #i82637# - From the perspective of the a11y API the default character
    // attributes are the character attributes, which are set at the paragraph style
    // of the paragraph. The character attributes set at the automatic paragraph
    // style of the paragraph are treated as run attributes.
    //    pTextNode->SwContentNode::GetAttr( *pSet );
    // get default paragraph attributes, if needed, and merge these into <pSet>
    if ( !bOnlyCharAttrs )
    {
        SfxItemSetFixed<RES_PARATR_BEGIN, RES_PARATR_END - 1,
                        RES_FRMATR_BEGIN, RES_FRMATR_END - 1>
             aParaSet( const_cast<SwAttrPool&>(pTextNode->GetDoc().GetAttrPool()) );
        pTextNode->SwContentNode::GetAttr( aParaSet );
        pSet->Put( aParaSet );
    }
    // get default character attributes and merge these into <pSet>
    OSL_ENSURE( pTextNode->GetTextColl(),
            "<SwAccessibleParagraph::_getDefaultAttributesImpl(..)> - missing paragraph style. Serious defect!" );
    if ( pTextNode->GetTextColl() )
    {
        SfxItemSetFixed<RES_CHRATR_BEGIN, RES_CHRATR_END - 1>
            aCharSet( const_cast<SwAttrPool&>(pTextNode->GetDoc().GetAttrPool()) );
        SetPutRecursive( aCharSet, pTextNode->GetTextColl()->GetAttrSet() );
        pSet->Put( aCharSet );
    }

    // build-up sequence containing the run attributes <rDefAttrSeq>
    tAccParaPropValMap aDefAttrSeq;
    {
        const SfxItemPropertyMap& rPropMap =
                    aSwMapProvider.GetPropertySet( PROPERTY_MAP_TEXT_CURSOR )->getPropertyMap();
        for ( const auto pEntry : rPropMap.getPropertyEntries() )
        {
            const SfxPoolItem* pItem = pSet->GetItem( pEntry->nWID );
            if ( pItem )
            {
                uno::Any aVal;
                pItem->QueryValue( aVal, pEntry->nMemberId );

                PropertyValue rPropVal;
                rPropVal.Name = pEntry->aName;
                rPropVal.Value = std::move(aVal);
                rPropVal.Handle = -1;
                rPropVal.State = beans::PropertyState_DEFAULT_VALUE;

                aDefAttrSeq[rPropVal.Name] = rPropVal;
            }
        }

        // #i72800#
        // add property value entry for the paragraph style
        if ( !bOnlyCharAttrs && pTextNode->GetTextColl() )
        {
            if ( aDefAttrSeq.find( UNO_NAME_PARA_STYLE_NAME ) == aDefAttrSeq.end() )
            {
                PropertyValue rPropVal;
                rPropVal.Name = UNO_NAME_PARA_STYLE_NAME;
                rPropVal.Value <<= pTextNode->GetTextColl()->GetName().toString();
                rPropVal.Handle = -1;
                rPropVal.State = beans::PropertyState_DEFAULT_VALUE;

                aDefAttrSeq[rPropVal.Name] = rPropVal;
            }
        }

        // #i73371#
        // resolve value text::WritingMode2::PAGE of property value entry WritingMode
        if ( !bOnlyCharAttrs && GetFrame() )
        {
            tAccParaPropValMap::iterator aIter = aDefAttrSeq.find( UNO_NAME_WRITING_MODE );
            if ( aIter != aDefAttrSeq.end() )
            {
                PropertyValue rPropVal( aIter->second );
                sal_Int16 nVal = rPropVal.Value.get<sal_Int16>();
                if ( nVal == text::WritingMode2::PAGE )
                {
                    const SwFrame* pUpperFrame( GetFrame()->GetUpper() );
                    while ( pUpperFrame )
                    {
                        if ( pUpperFrame->GetType() &
                               ( SwFrameType::Page | SwFrameType::Fly | SwFrameType::Section | SwFrameType::Tab | SwFrameType::Cell ) )
                        {
                            if ( pUpperFrame->IsVertical() )
                            {
                                nVal = text::WritingMode2::TB_RL;
                            }
                            else if ( pUpperFrame->IsRightToLeft() )
                            {
                                nVal = text::WritingMode2::RL_TB;
                            }
                            else
                            {
                                nVal = text::WritingMode2::LR_TB;
                            }
                            rPropVal.Value <<= nVal;
                            aDefAttrSeq[rPropVal.Name] = rPropVal;
                            break;
                        }

                        if ( pUpperFrame->IsFlyFrame() )
                        {
                            pUpperFrame = static_cast<const SwFlyFrame*>(pUpperFrame)->GetAnchorFrame();
                        }
                        else
                        {
                            pUpperFrame = pUpperFrame->GetUpper();
                        }
                    }
                }
            }
        }
    }

    if ( !aRequestedAttributes.hasElements() )
    {
        rDefAttrSeq = std::move(aDefAttrSeq);
    }
    else
    {
        for( const OUString& rReqAttr : aRequestedAttributes )
        {
            tAccParaPropValMap::const_iterator const aIter = aDefAttrSeq.find( rReqAttr );
            if ( aIter != aDefAttrSeq.end() )
            {
                rDefAttrSeq[ aIter->first ] = aIter->second;
            }
        }
    }
}

uno::Sequence< PropertyValue > SwAccessibleParagraph::getDefaultAttributes(
        const uno::Sequence< OUString >& aRequestedAttributes )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    tAccParaPropValMap aDefAttrSeq;
    _getDefaultAttributesImpl( aRequestedAttributes, aDefAttrSeq );

    // #i92233#
    static constexpr OUString sMMToPixelRatio = u"MMToPixelRatio"_ustr;
    bool bProvideMMToPixelRatio( !aRequestedAttributes.hasElements() ||
                                 (comphelper::findValue(aRequestedAttributes, sMMToPixelRatio) != -1) );

    uno::Sequence< PropertyValue > aValues( aDefAttrSeq.size() +
                                            ( bProvideMMToPixelRatio ? 1 : 0 ) );
    auto pValues = aValues.getArray();
    std::transform(aDefAttrSeq.begin(), aDefAttrSeq.end(), pValues,
        [](const auto& rEntry) -> PropertyValue { return rEntry.second; });

    // #i92233#
    if ( bProvideMMToPixelRatio )
    {
        PropertyValue rPropVal;
        rPropVal.Name = sMMToPixelRatio;
        const Size a100thMMSize( 1000, 1000 );
        const Size aPixelSize = GetMap()->LogicToPixel( a100thMMSize );
        const float fRatio = (static_cast<float>(a100thMMSize.Width())/100)/aPixelSize.Width();
        rPropVal.Value <<= fRatio;
        rPropVal.Handle = -1;
        rPropVal.State = beans::PropertyState_DEFAULT_VALUE;
        pValues[ aValues.getLength() - 1 ] = std::move(rPropVal);
    }

    return aValues;
}

void SwAccessibleParagraph::_getRunAttributesImpl(
        const sal_Int32 nIndex,
        const uno::Sequence< OUString >& aRequestedAttributes,
        tAccParaPropValMap& rRunAttrSeq )
{
    // create PaM for character at position <nIndex>
    std::optional<SwPaM> pPaM;
    const TextFrameIndex nCorePos(GetPortionData().GetCoreViewPosition(nIndex));
    const SwTextFrame* const pFrame = GetTextFrame();
    SwPosition const aModelPos(pFrame->MapViewToModelPos(nCorePos));
    SwTextNode *const pTextNode(aModelPos.GetNode().GetTextNode());
    {
        SwPosition const aEndPos(*pTextNode,
            aModelPos.GetContentIndex() == pTextNode->Len()
                ? pTextNode->Len() // ???
                : aModelPos.GetContentIndex() + 1);
        pPaM.emplace(aModelPos, aEndPos);
    }

    // retrieve character attributes for the created PaM <pPaM>
    SfxItemSetFixed<RES_CHRATR_BEGIN, RES_CHRATR_END -1> aSet( pPaM->GetDoc().GetAttrPool() );
    // #i82637#
    // From the perspective of the a11y API the character attributes, which
    // are set at the automatic paragraph style of the paragraph, are treated
    // as run attributes.
    //    SwXTextCursor::GetCursorAttr( *pPaM, aSet, sal_True, sal_True );
    // get character attributes from automatic paragraph style and merge these into <aSet>
    if ( pTextNode->HasSwAttrSet() )
    {
        SfxItemSetFixed<RES_CHRATR_BEGIN, RES_CHRATR_END -1> aAutomaticParaStyleCharAttrs( pPaM->GetDoc().GetAttrPool());
        aAutomaticParaStyleCharAttrs.Put( *(pTextNode->GetpSwAttrSet()), false );
        aSet.Put( aAutomaticParaStyleCharAttrs );
    }
    // get character attributes at <pPaM> and merge these into <aSet>
    {
        SfxItemSetFixed<RES_CHRATR_BEGIN, RES_CHRATR_END -1> aCharAttrsAtPaM( pPaM->GetDoc().GetAttrPool() );
        SwUnoCursorHelper::GetCursorAttr(*pPaM, aCharAttrsAtPaM, true);
        aSet.Put( aCharAttrsAtPaM );
    }

    // build-up sequence containing the run attributes <rRunAttrSeq>
    {
        tAccParaPropValMap aRunAttrSeq;
        {
            tAccParaPropValMap aDefAttrSeq;
            uno::Sequence< OUString > aDummy;
            _getDefaultAttributesImpl( aDummy, aDefAttrSeq, true ); // #i82637#

            const SfxItemPropertyMap& rPropMap =
                    aSwMapProvider.GetPropertySet( PROPERTY_MAP_TEXT_CURSOR )->getPropertyMap();
            for ( const auto pEntry : rPropMap.getPropertyEntries() )
            {
                const SfxPoolItem* pItem( nullptr );
                // #i82637# - Found character attributes, whose value equals the value of
                // the corresponding default character attributes, are excluded.
                if ( aSet.GetItemState( pEntry->nWID, true, &pItem ) == SfxItemState::SET )
                {
                    uno::Any aVal;
                    pItem->QueryValue( aVal, pEntry->nMemberId );

                    PropertyValue rPropVal;
                    rPropVal.Name = pEntry->aName;
                    rPropVal.Value = std::move(aVal);
                    rPropVal.Handle = -1;
                    rPropVal.State = PropertyState_DIRECT_VALUE;

                    tAccParaPropValMap::const_iterator aDefIter =
                                            aDefAttrSeq.find( rPropVal.Name );
                    if ( aDefIter == aDefAttrSeq.end() ||
                         rPropVal.Value != aDefIter->second.Value )
                    {
                        aRunAttrSeq[rPropVal.Name] = rPropVal;
                    }
                }
            }
        }

        if ( !aRequestedAttributes.hasElements() )
        {
            rRunAttrSeq = std::move(aRunAttrSeq);
        }
        else
        {
            for( const OUString& rReqAttr : aRequestedAttributes )
            {
                tAccParaPropValMap::iterator aIter = aRunAttrSeq.find( rReqAttr );
                if ( aIter != aRunAttrSeq.end() )
                {
                    rRunAttrSeq[ (*aIter).first ] = (*aIter).second;
                }
            }
        }
    }
}

uno::Sequence< PropertyValue > SwAccessibleParagraph::getRunAttributes(
        sal_Int32 nIndex,
        const uno::Sequence< OUString >& aRequestedAttributes )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    {
        const OUString& rText = GetString();
        if (!IsValidPosition(nIndex, rText.getLength()))
        {
            throw lang::IndexOutOfBoundsException();
        }
    }

    tAccParaPropValMap aRunAttrSeq;
    _getRunAttributesImpl( nIndex, aRequestedAttributes, aRunAttrSeq );

    return comphelper::mapValuesToSequence( aRunAttrSeq );
}

void SwAccessibleParagraph::_getSupplementalAttributesImpl(
        const uno::Sequence< OUString >& aRequestedAttributes,
        tAccParaPropValMap& rSupplementalAttrSeq )
{
    const SwTextFrame* const pFrame = GetTextFrame();
    const SwTextNode *const pTextNode(pFrame->GetTextNodeForParaProps());
    SfxItemSetFixed<
                RES_PARATR_LINESPACING, RES_PARATR_ADJUST,
                RES_PARATR_TABSTOP, RES_PARATR_TABSTOP,
                RES_PARATR_NUMRULE, RES_PARATR_NUMRULE,
                RES_PARATR_LIST_BEGIN, RES_PARATR_LIST_END - 1,
                RES_MARGIN_FIRSTLINE, RES_MARGIN_RIGHT,
                RES_UL_SPACE, RES_UL_SPACE>
        aSet( const_cast<SwAttrPool&>(pTextNode->GetDoc().GetAttrPool()) );

    if ( pTextNode->HasBullet() || pTextNode->HasNumber() )
    {
        aSet.Put( pTextNode->GetAttr(RES_PARATR_LIST_LEVEL) );
        aSet.Put( pTextNode->GetAttr(RES_PARATR_LIST_ISCOUNTED) );
    }
    aSet.Put( pTextNode->SwContentNode::GetAttr(RES_UL_SPACE) );
    aSet.Put( pTextNode->SwContentNode::GetAttr(RES_MARGIN_FIRSTLINE) );
    aSet.Put( pTextNode->SwContentNode::GetAttr(RES_MARGIN_TEXTLEFT) );
    aSet.Put( pTextNode->SwContentNode::GetAttr(RES_MARGIN_RIGHT) );
    aSet.Put( pTextNode->SwContentNode::GetAttr(RES_PARATR_ADJUST) );

    tAccParaPropValMap aSupplementalAttrSeq;
    {
        std::span<const SfxItemPropertyMapEntry> pPropMap(
                aSwMapProvider.GetPropertyMapEntries( PROPERTY_MAP_ACCESSIBILITY_TEXT_ATTRIBUTE ) );
        for (const auto & rEntry : pPropMap)
        {
            // For a paragraph, list level property is not set but when queried the returned default
            // value is 0, exactly the same value of top level list item; that prevents using
            // list level property for discerning simple paragraph from list item;
            // the following check allows not to return the list level property at all
            // when we are dealing with a simple paragraph
            if ((rEntry.nWID == RES_PARATR_LIST_LEVEL || rEntry.nWID == RES_PARATR_LIST_ISCOUNTED) &&
                !aSet.HasItem( rEntry.nWID ))
                continue;

            const SfxPoolItem* pItem = aSet.GetItem( rEntry.nWID );
            if ( pItem )
            {
                uno::Any aVal;
                pItem->QueryValue( aVal, rEntry.nMemberId );

                PropertyValue rPropVal;
                rPropVal.Name = rEntry.aName;
                rPropVal.Value = std::move(aVal);
                rPropVal.Handle = -1;
                rPropVal.State = beans::PropertyState_DEFAULT_VALUE;

                aSupplementalAttrSeq[rPropVal.Name] = rPropVal;
            }
        }
    }

    for( const OUString& rSupplementalAttr : aRequestedAttributes )
    {
        tAccParaPropValMap::const_iterator const aIter = aSupplementalAttrSeq.find( rSupplementalAttr );
        if ( aIter != aSupplementalAttrSeq.end() )
        {
            rSupplementalAttrSeq[ aIter->first ] = aIter->second;
        }
    }
}

void SwAccessibleParagraph::_correctValues( const sal_Int32 nIndex,
                                            std::vector< PropertyValue >& rValues)
{
    PropertyValue ChangeAttr, ChangeAttrColor;

    const SwRangeRedline* pRedline = GetRedlineAtIndex();
    if ( pRedline )
    {

        const SwModuleOptions* pOpt = SwModule::get()->GetModuleConfig();
        AuthorCharAttr aChangeAttr;
        if ( pOpt )
        {
            switch( pRedline->GetType())
            {
            case RedlineType::Insert:
                aChangeAttr = pOpt->GetInsertAuthorAttr();
                break;
            case RedlineType::Delete:
                aChangeAttr = pOpt->GetDeletedAuthorAttr();
                break;
            case RedlineType::Format:
                aChangeAttr = pOpt->GetFormatAuthorAttr();
                break;
            default: break;
            }
        }
        switch( aChangeAttr.m_nItemId )
        {
        case SID_ATTR_CHAR_WEIGHT:
            ChangeAttr.Name = UNO_NAME_CHAR_WEIGHT;
            ChangeAttr.Value <<= awt::FontWeight::BOLD;
            break;
        case SID_ATTR_CHAR_POSTURE:
            ChangeAttr.Name = UNO_NAME_CHAR_POSTURE;
            ChangeAttr.Value <<= awt::FontSlant_ITALIC; //char posture
            break;
        case SID_ATTR_CHAR_STRIKEOUT:
            ChangeAttr.Name = UNO_NAME_CHAR_STRIKEOUT;
            ChangeAttr.Value <<= awt::FontStrikeout::SINGLE; //char strikeout
            break;
        case SID_ATTR_CHAR_UNDERLINE:
            ChangeAttr.Name = UNO_NAME_CHAR_UNDERLINE;
            ChangeAttr.Value <<= aChangeAttr.m_nAttr; //underline line
            break;
        }
        if( aChangeAttr.m_nColor != COL_NONE_COLOR )
        {
            if( aChangeAttr.m_nItemId == SID_ATTR_BRUSH )
            {
                ChangeAttrColor.Name = UNO_NAME_CHAR_BACK_COLOR;
                if( aChangeAttr.m_nColor == COL_TRANSPARENT )//char backcolor
                    ChangeAttrColor.Value <<= COL_BLUE;
                else
                    ChangeAttrColor.Value <<= aChangeAttr.m_nColor;
            }
            else
            {
                ChangeAttrColor.Name = UNO_NAME_CHAR_COLOR;
                if( aChangeAttr.m_nColor == COL_TRANSPARENT )//char color
                    ChangeAttrColor.Value <<= COL_BLUE;
                else
                    ChangeAttrColor.Value <<= aChangeAttr.m_nColor;
            }
        }
    }

    // sw_redlinehide: this function only needs SwWrongList for 1 character,
    // and the end is excluded by InWrongWord(),
    // so it ought to work to just pick the wrong-list/node that contains
    // the character following the given nIndex
    const SwTextFrame* const pFrame = GetTextFrame();
    TextFrameIndex const nCorePos(GetPortionData().GetCoreViewPosition(nIndex));
    std::pair<SwTextNode*, sal_Int32> pos(pFrame->MapViewToModel(nCorePos));
    if (pos.first->Len() == pos.second
        && nCorePos != TextFrameIndex(pFrame->GetText().getLength()))
    {
        pos = pFrame->MapViewToModel(nCorePos + TextFrameIndex(1)); // try this one instead
        assert(pos.first->Len() != pos.second);
    }

    sal_Int32 nValues = rValues.size();
    for (sal_Int32 i = 0;  i < nValues;  ++i)
    {
        PropertyValue& rValue = rValues[i];

        if (rValue.Name == ChangeAttr.Name )
        {
            rValue.Value = ChangeAttr.Value;
            continue;
        }

        if (rValue.Name == ChangeAttrColor.Name )
        {
            rValue.Value = ChangeAttrColor.Value;
            continue;
        }

        //back color
        if (rValue.Name == UNO_NAME_CHAR_BACK_COLOR)
        {
            uno::Any &anyChar = rValue.Value;
            Color backColor;
            anyChar >>= backColor;
            if (COL_AUTO == backColor)
            {
                uno::Reference<XAccessibleComponent> xComponent(this);
                if (xComponent.is())
                {
                    sal_uInt32 crBack = static_cast<sal_uInt32>(xComponent->getBackground());
                    rValue.Value <<= crBack;
                }
            }
            continue;
        }

        //char color
        if (rValue.Name == UNO_NAME_CHAR_COLOR)
        {
            if( GetPortionData().IsInGrayPortion( nIndex ) )
                rValue.Value <<= GetCursorShell()->GetViewOptions()->GetFieldShadingsColor();
            uno::Any &anyChar = rValue.Value;
            Color charColor;
            anyChar >>= charColor;

            if( COL_AUTO == charColor )
            {
                uno::Reference<XAccessibleComponent> xComponent(this);
                if (xComponent.is())
                {
                    Color cr(ColorTransparency, xComponent->getBackground());
                    sal_uInt32 crChar = sal_uInt32(cr.IsDark() ? COL_WHITE : COL_BLACK);
                    rValue.Value <<= crChar;
                }
            }
            continue;
        }

        // UnderLineColor
        if (rValue.Name == UNO_NAME_CHAR_UNDERLINE_COLOR)
        {
            uno::Any &anyChar = rValue.Value;
            Color underlineColor;
            anyChar >>= underlineColor;
            if ( COL_AUTO == underlineColor )
            {
                uno::Reference<XAccessibleComponent> xComponent(this);
                if (xComponent.is())
                {
                    Color cr(ColorTransparency, xComponent->getBackground());
                    underlineColor = cr.IsDark() ? COL_WHITE : COL_BLACK;
                    rValue.Value <<= underlineColor;
                }
            }

            continue;
        }

        //tab stop
        if (rValue.Name == UNO_NAME_TABSTOPS)
        {
            css::uno::Sequence< css::style::TabStop > tabs = GetCurrentTabStop( nIndex );
            if( !tabs.hasElements() )
            {
                css::style::TabStop ts;
                css::awt::Rectangle rc0 = getCharacterBounds(0);
                css::awt::Rectangle rc1 = getCharacterBounds(nIndex);
                if( rc1.X - rc0.X >= 48 )
                    ts.Position = (rc1.X - rc0.X) - (rc1.X - rc0.X - 48)% 47 + 47;
                else
                    ts.Position = 48;
                ts.DecimalChar = ' ';
                ts.FillChar = ' ';
                ts.Alignment = css::style::TabAlign_LEFT;
                tabs = { ts };
            }
            rValue.Value <<= tabs;
            continue;
        }

        //footnote & endnote
        if (rValue.Name == UNO_NAME_CHAR_ESCAPEMENT)
        {
            if ( GetPortionData().IsIndexInFootnode(nIndex) )
            {
                rValue.Value <<= sal_Int32(101);
            }
            continue;
        }
    }
}

awt::Rectangle SwAccessibleParagraph::getCharacterBounds(
    sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    // #i12332# The position after the string needs special treatment.
    // IsValidChar -> IsValidPosition
    if( ! (IsValidPosition( nIndex, GetString().getLength() ) ) )
        throw lang::IndexOutOfBoundsException();

    // #i12332#
    bool bBehindText = false;
    if ( nIndex == GetString().getLength() )
        bBehindText = true;

    // get model position & prepare GetCharRect() arguments
    SwCursorMoveState aMoveState;
    aMoveState.m_bRealHeight = true;
    aMoveState.m_bRealWidth = true;
    SwSpecialPos aSpecialPos;
    const SwTextFrame* const pFrame = GetTextFrame();

    /**  #i12332# FillSpecialPos does not accept nIndex ==
         GetString().getLength(). In that case nPos is set to the
         length of the string in the core. This way GetCharRect
         returns the rectangle for a cursor at the end of the
         paragraph. */
    const TextFrameIndex nPos = bBehindText
        ? TextFrameIndex(pFrame->GetText().getLength())
        : GetPortionData().FillSpecialPos(nIndex, aSpecialPos, aMoveState.m_pSpecialPos );

    // call GetCharRect
    SwRect aCoreRect;
    SwPosition aPosition(pFrame->MapViewToModelPos(nPos));
    GetFrame()->GetCharRect( aCoreRect, aPosition, &aMoveState );

    // translate core coordinates into accessibility coordinates
    vcl::Window *pWin = GetWindow();
    if (!pWin)
    {
        throw uno::RuntimeException(u"no Window"_ustr, getXWeak());
    }

    tools::Rectangle aScreenRect( GetMap()->CoreToPixel( aCoreRect ));
    SwRect aFrameLogBounds( GetBounds( *(GetMap()) ) ); // twip rel to doc root

    Point aFramePixPos( GetMap()->CoreToPixel( aFrameLogBounds ).TopLeft() );
    aScreenRect.Move( -aFramePixPos.getX(), -aFramePixPos.getY() );

    return vcl::unohelper::ConvertToAWTRect(aScreenRect);
}

sal_Int32 SwAccessibleParagraph::getCharacterCount()
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    return GetString().getLength();
}

sal_Int32 SwAccessibleParagraph::getIndexAtPoint( const awt::Point& rPoint )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    // construct Point (translate into layout coordinates)
    vcl::Window *pWin = GetWindow();
    if (!pWin)
    {
        throw uno::RuntimeException(u"no Window"_ustr, getXWeak());
    }
    SwRect aLogBounds( GetBounds( *(GetMap()), GetFrame() ) ); // twip rel to doc root
    Point aPixPos( GetMap()->CoreToPixel( aLogBounds ).TopLeft() );
    const Point aPoint(rPoint.X + aPixPos.getX(), rPoint.Y + aPixPos.getY());
    Point aCorePoint( GetMap()->PixelToCore( aPoint ) );
    if( !aLogBounds.Contains( aCorePoint ) )
    {
        // #i12332# rPoint is may also be in rectangle returned by
        // getCharacterBounds(getCharacterCount()

        awt::Rectangle aRectEndPos =
            getCharacterBounds(getCharacterCount());

        if (rPoint.X - aRectEndPos.X >= 0 &&
            rPoint.X - aRectEndPos.X < aRectEndPos.Width &&
            rPoint.Y - aRectEndPos.Y >= 0 &&
            rPoint.Y - aRectEndPos.Y < aRectEndPos.Height)
            return getCharacterCount();

        return -1;
    }

    // ask core for position
    OSL_ENSURE( GetFrame() != nullptr, "The text frame has vanished!" );
    OSL_ENSURE( GetFrame()->IsTextFrame(), "The text frame has mutated!" );
    const SwTextFrame* pFrame = GetTextFrame();
    // construct SwPosition (where GetModelPositionForViewPoint() will put the result into)
    SwTextNode* pNode = const_cast<SwTextNode*>(pFrame->GetTextNodeFirst());
    SwPosition aPos(*pNode, 0);
    SwCursorMoveState aMoveState;
    aMoveState.m_bPosMatchesBounds = true;
    const bool bSuccess = pFrame->GetModelPositionForViewPoint( &aPos, aCorePoint, &aMoveState );

    TextFrameIndex nIndex = pFrame->MapModelToViewPos(aPos);
    if (TextFrameIndex(0) < nIndex)
    {
        assert(bSuccess);
        SwRect aResultRect;
        pFrame->GetCharRect( aResultRect, aPos );
        bool bVert = pFrame->IsVertical();
        bool bR2L = pFrame->IsRightToLeft();

        if ( (!bVert && aResultRect.Pos().getX() > aCorePoint.getX()) ||
             ( bVert && aResultRect.Pos().getY() > aCorePoint.getY()) ||
             ( bR2L  && aResultRect.Right()   < aCorePoint.getX()) )
        {
            SwPosition aPosPrev(pFrame->MapViewToModelPos(nIndex - TextFrameIndex(1)));
            SwRect aResultRectPrev;
            pFrame->GetCharRect( aResultRectPrev, aPosPrev );
            if ( (!bVert && aResultRectPrev.Pos().getX() < aCorePoint.getX() && aResultRect.Pos().getY() == aResultRectPrev.Pos().getY()) ||
                 ( bVert && aResultRectPrev.Pos().getY() < aCorePoint.getY() && aResultRect.Pos().getX() == aResultRectPrev.Pos().getX()) ||
                 (  bR2L && aResultRectPrev.Right()   > aCorePoint.getX() && aResultRect.Pos().getY() == aResultRectPrev.Pos().getY()) )
            {
                --nIndex;
            }
        }
    }

    return bSuccess
        ? GetPortionData().GetAccessiblePosition(nIndex)
        : -1;
}

OUString SwAccessibleParagraph::getSelectedText()
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    sal_Int32 nStart, nEnd;
    bool bSelected = GetSelection( nStart, nEnd );
    return bSelected
           ? GetString().copy( nStart, nEnd - nStart )
           : OUString();
}

sal_Int32 SwAccessibleParagraph::getSelectionStart()
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    sal_Int32 nStart, nEnd;
    GetSelection( nStart, nEnd );
    return nStart;
}

sal_Int32 SwAccessibleParagraph::getSelectionEnd()
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    sal_Int32 nStart, nEnd;
    GetSelection( nStart, nEnd );
    return nEnd;
}

sal_Bool SwAccessibleParagraph::setSelection( sal_Int32 nStartIndex, sal_Int32 nEndIndex )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    // parameter checking
    sal_Int32 nLength = GetString().getLength();
    if ( ! IsValidRange( nStartIndex, nEndIndex, nLength ) )
    {
        throw lang::IndexOutOfBoundsException();
    }

    bool bRet = false;

    // get cursor shell
    SwCursorShell* pCursorShell = GetCursorShell();
    if( pCursorShell != nullptr )
    {
        // create pam for selection
        const SwTextFrame* const pFrame = GetTextFrame();
        TextFrameIndex const nStart(GetPortionData().GetCoreViewPosition(nStartIndex));
        TextFrameIndex const nEnd(GetPortionData().GetCoreViewPosition(nEndIndex));
        SwPaM aPaM(pFrame->MapViewToModelPos(nStart));
        aPaM.SetMark();
        *aPaM.GetPoint() = pFrame->MapViewToModelPos(nEnd);

        // set PaM at cursor shell
        bRet = Select( aPaM );
    }

    return bRet;
}

OUString SwAccessibleParagraph::getText()
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    return GetString();
}

OUString SwAccessibleParagraph::getTextRange(
    sal_Int32 nStartIndex, sal_Int32 nEndIndex )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    OUString sText( GetString() );

    if ( !IsValidRange( nStartIndex, nEndIndex, sText.getLength() ) )
        throw lang::IndexOutOfBoundsException();

    OrderRange( nStartIndex, nEndIndex );
    return sText.copy(nStartIndex, nEndIndex-nStartIndex );
}

TextSegment SwAccessibleParagraph::getTextAtIndex(sal_Int32 nIndex, sal_Int16 nTextType)
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    TextSegment aResult;
    aResult.SegmentStart = -1;
    aResult.SegmentEnd = -1;

    const OUString rText = GetString();
    // implement the silly specification that first position after
    // text must return an empty string, rather than throwing an
    // IndexOutOfBoundsException, except for LINE, where the last
    // line is returned
    if( nIndex == rText.getLength() && AccessibleTextType::LINE != nTextType )
        return aResult;

    // with error checking
    i18n::Boundary aBound;
    bool bWord = GetTextBoundary( aBound, rText, nIndex, nTextType );

    OSL_ENSURE( aBound.startPos >= 0,               "illegal boundary" );
    OSL_ENSURE( aBound.startPos <= aBound.endPos,   "illegal boundary" );

    // return word (if present)
    if ( bWord )
    {
        aResult.SegmentText = rText.copy( aBound.startPos, aBound.endPos - aBound.startPos );
        aResult.SegmentStart = aBound.startPos;
        aResult.SegmentEnd = aBound.endPos;
    }

    return aResult;
}

TextSegment SwAccessibleParagraph::getTextBeforeIndex(sal_Int32 nIndex, sal_Int16 nTextType)
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    const OUString rText = GetString();

    TextSegment aResult;
    aResult.SegmentStart = -1;
    aResult.SegmentEnd = -1;
    //If nIndex = 0, then nobefore text so return -1 directly.
    if( nIndex == 0 )
            return aResult;
    //Tab will be return when call WORDTYPE

    // get starting pos
    i18n::Boundary aBound;
    if (nIndex ==  rText.getLength())
        aBound.startPos = aBound.endPos = nIndex;
    else
    {
        bool bTmp = GetTextBoundary( aBound, rText, nIndex, nTextType );

        if ( ! bTmp )
            aBound.startPos = aBound.endPos = nIndex;
    }

    // now skip to previous word
    if (nTextType == AccessibleTextType::WORD || nTextType == AccessibleTextType::SENTENCE)
    {
        i18n::Boundary preBound = aBound;
        while(preBound.startPos==aBound.startPos && nIndex > 0)
        {
            nIndex = std::min(nIndex, preBound.startPos);
            if (nIndex <= 0) break;
            rText.iterateCodePoints(&nIndex, -1);
            GetTextBoundary( preBound, rText, nIndex, nTextType );
        }
        //if (nIndex>0)
        if (nIndex>=0)
        //Tab will be return when call WORDTYPE
        {
            aResult.SegmentText = rText.copy( preBound.startPos, preBound.endPos - preBound.startPos );
            aResult.SegmentStart = preBound.startPos;
            aResult.SegmentEnd = preBound.endPos;
        }
    }
    else
    {
        bool bWord = false;
        while( !bWord )
        {
            nIndex = std::min(nIndex, aBound.startPos);
            if (nIndex > 0)
            {
                rText.iterateCodePoints(&nIndex, -1);
                bWord = GetTextBoundary( aBound, rText, nIndex, nTextType );
            }
            else
                break;  // exit if beginning of string is reached
        }

        if (bWord && nIndex<rText.getLength())
        {
            aResult.SegmentText = rText.copy( aBound.startPos, aBound.endPos - aBound.startPos );
            aResult.SegmentStart = aBound.startPos;
            aResult.SegmentEnd = aBound.endPos;
        }
    }
    return aResult;
}

TextSegment SwAccessibleParagraph::getTextBehindIndex(sal_Int32 nIndex, sal_Int16 nTextType)
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    TextSegment aResult;
    aResult.SegmentStart = -1;
    aResult.SegmentEnd = -1;
    const OUString rText = GetString();

    // implement the silly specification that first position after
    // text must return an empty string, rather than throwing an
    // IndexOutOfBoundsException
    if( nIndex == rText.getLength() )
        return aResult;

    // get first word, then skip to next word
    i18n::Boundary aBound;
    GetTextBoundary( aBound, rText, nIndex, nTextType );
    bool bWord = false;
    while( !bWord )
    {
        nIndex = std::max(sal_Int32(nIndex + 1), aBound.endPos);
        if( nIndex < rText.getLength() )
            bWord = GetTextBoundary( aBound, rText, nIndex, nTextType );
        else
            break;  // exit if end of string is reached
    }

    if ( bWord )
    {
        aResult.SegmentText = rText.copy( aBound.startPos, aBound.endPos - aBound.startPos );
        aResult.SegmentStart = aBound.startPos;
        aResult.SegmentEnd = aBound.endPos;
    }

/*
        sal_Bool bWord = sal_False;
    bWord = GetTextBoundary( aBound, rText, nIndex, nTextType );

        if (nTextType == AccessibleTextType::WORD)
        {
                Boundary nexBound=aBound;

        // real current word
        if( nIndex <= aBound.endPos && nIndex >= aBound.startPos )
        {
            while(nexBound.endPos==aBound.endPos&&nIndex<rText.getLength())
            {
                // nIndex = std::max( (sal_Int32)(nIndex), nexBound.endPos) + 1;
                nIndex = std::max( (sal_Int32)(nIndex), nexBound.endPos) ;
                const sal_Unicode* pStr = rText.getStr();
                if (pStr)
                {
                    if( pStr[nIndex] == sal_Unicode(' ') )
                        nIndex++;
                }
                if( nIndex < rText.getLength() )
                {
                    bWord = GetTextBoundary( nexBound, rText, nIndex, nTextType );
                }
            }
        }

        if (bWord && nIndex<rText.getLength())
        {
            aResult.SegmentText = rText.copy( nexBound.startPos, nexBound.endPos - nexBound.startPos );
            aResult.SegmentStart = nexBound.startPos;
            aResult.SegmentEnd = nexBound.endPos;
        }

    }
    else
    {
        bWord = sal_False;
        while( !bWord )
        {
            nIndex = std::max( (sal_Int32)(nIndex+1), aBound.endPos );
            if( nIndex < rText.getLength() )
            {
                bWord = GetTextBoundary( aBound, rText, nIndex, nTextType );
            }
            else
                break;  // exit if end of string is reached
        }
        if (bWord && nIndex<rText.getLength())
        {
            aResult.SegmentText = rText.copy( aBound.startPos, aBound.endPos - aBound.startPos );
            aResult.SegmentStart = aBound.startPos;
            aResult.SegmentEnd = aBound.endPos;
        }
    }
*/
    return aResult;
}

sal_Bool SwAccessibleParagraph::copyText( sal_Int32 nStartIndex, sal_Int32 nEndIndex )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    // select and copy (through dispatch mechanism)
    setSelection( nStartIndex, nEndIndex );
    ExecuteAtViewShell( SID_COPY );
    return true;
}

sal_Bool SwAccessibleParagraph::scrollSubstringTo( sal_Int32 nStartIndex,
    sal_Int32 nEndIndex, AccessibleScrollType aScrollType )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    // parameter checking
    sal_Int32 nLength = GetString().getLength();
    if ( ! IsValidRange( nStartIndex, nEndIndex, nLength ) )
        throw lang::IndexOutOfBoundsException();

    vcl::Window *pWin = GetWindow();
    if ( ! pWin )
        throw uno::RuntimeException(u"no Window"_ustr, getXWeak());

    /* Start and end character bounds, in pixels, relative to the paragraph */
    awt::Rectangle startR, endR;
    startR = getCharacterBounds(nStartIndex);
    endR = getCharacterBounds(nEndIndex);

    /* Adjust points to fit the bounding box of both bounds. */
    Point sP(std::min(startR.X, endR.X), startR.Y);
    Point eP(std::max(startR.X + startR.Width, endR.X + endR.Width), endR.Y + endR.Height);

    /* Offset the values relative to the view shell frame */
    SwRect aFrameLogBounds( GetBounds( *(GetMap()) ) ); // twip rel to doc root
    Point aFramePixPos( GetMap()->CoreToPixel( aFrameLogBounds ).TopLeft() );
    sP += aFramePixPos;
    eP += aFramePixPos;

    Point startPoint(GetMap()->PixelToCore(sP));
    Point endPoint(GetMap()->PixelToCore(eP));

    switch (aScrollType)
    {
#ifdef notyet
        case AccessibleScrollType_SCROLL_TOP_LEFT:
            break;
        case AccessibleScrollType_SCROLL_BOTTOM_RIGHT:
            break;
        case AccessibleScrollType_SCROLL_TOP_EDGE:
            break;
        case AccessibleScrollType_SCROLL_BOTTOM_EDGE:
            break;
        case AccessibleScrollType_SCROLL_LEFT_EDGE:
            break;
        case AccessibleScrollType_SCROLL_RIGHT_EDGE:
            break;
#endif
        case AccessibleScrollType_SCROLL_ANYWHERE:
            break;
        default:
            return false;
    }

    const SwRect aRect(startPoint, endPoint);
    SwViewShell& rViewShell = GetMap()->GetShell();

    ScrollMDI(rViewShell, aRect, USHRT_MAX, USHRT_MAX);

    return true;
}

// XAccessibleEditableText

sal_Bool SwAccessibleParagraph::cutText( sal_Int32 nStartIndex, sal_Int32 nEndIndex )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    if( !IsEditableState() )
        return false;

    // select and cut (through dispatch mechanism)
    setSelection( nStartIndex, nEndIndex );
    ExecuteAtViewShell( SID_CUT );
    return true;
}

sal_Bool SwAccessibleParagraph::pasteText( sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    if( !IsEditableState() )
        return false;

    // select and paste (through dispatch mechanism)
    setSelection( nIndex, nIndex );
    ExecuteAtViewShell( SID_PASTE );
    return true;
}

sal_Bool SwAccessibleParagraph::deleteText( sal_Int32 nStartIndex, sal_Int32 nEndIndex )
{
    return replaceText( nStartIndex, nEndIndex, OUString() );
}

sal_Bool SwAccessibleParagraph::insertText( const OUString& sText, sal_Int32 nIndex )
{
    return replaceText( nIndex, nIndex, sText );
}

sal_Bool SwAccessibleParagraph::replaceText(
    sal_Int32 nStartIndex, sal_Int32 nEndIndex,
    const OUString& sReplacement )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    const OUString& rText = GetString();

    if( !IsValidRange( nStartIndex, nEndIndex, rText.getLength() ) )
        throw lang::IndexOutOfBoundsException();

    if( !IsEditableState() )
        return false;

    // translate positions
    TextFrameIndex nStart;
    TextFrameIndex nEnd;
    bool bSuccess = GetPortionData().GetEditableRange(
                                    nStartIndex, nEndIndex, nStart, nEnd );

    // edit only if the range is editable
    if( bSuccess )
    {
        const SwTextFrame* const pFrame = GetTextFrame();
        // create SwPosition for nStartIndex
        SwPosition aStartPos(pFrame->MapViewToModelPos(nStart));

        // create SwPosition for nEndIndex
        SwPosition aEndPos(pFrame->MapViewToModelPos(nEnd));

        // now create XTextRange as helper and set string
        const rtl::Reference<SwXTextRange> xRange(
            SwXTextRange::CreateXTextRange(
                const_cast<SwDoc&>(pFrame->GetDoc()), aStartPos, &aEndPos));
        xRange->setString(sReplacement);

        // delete portion data
        ClearPortionData();
    }

    return bSuccess;

}

sal_Bool SwAccessibleParagraph::setAttributes(
    sal_Int32 nStartIndex,
    sal_Int32 nEndIndex,
    const uno::Sequence<PropertyValue>& rAttributeSet )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    const OUString& rText = GetString();

    if( ! IsValidRange( nStartIndex, nEndIndex, rText.getLength() ) )
        throw lang::IndexOutOfBoundsException();

    if( !IsEditableState() )
        return false;

    // create a (dummy) text portion for the sole purpose of calling
    // setPropertyValue on it
    rtl::Reference<SwXTextPortion> xPortion = CreateUnoPortion( nStartIndex,
                                                              nEndIndex );

    // build sorted index array
    sal_Int32 nLength = rAttributeSet.getLength();
    const PropertyValue* pPairs = rAttributeSet.getConstArray();
    std::vector<sal_Int32> aIndices(nLength);
    std::iota(aIndices.begin(), aIndices.end(), 0);
    std::sort(aIndices.begin(), aIndices.end(), IndexCompare(pPairs));

    // create sorted sequences according to index array
    uno::Sequence< OUString > aNames( nLength );
    OUString* pNames = aNames.getArray();
    uno::Sequence< uno::Any > aValues( nLength );
    uno::Any* pValues = aValues.getArray();
    for (sal_Int32 i = 0; i < nLength; ++i)
    {
        const PropertyValue& rVal = pPairs[aIndices[i]];
        pNames[i]  = rVal.Name;
        pValues[i] = rVal.Value;
    }
    aIndices.clear();

    // now set the values
    bool bRet = true;
    try
    {
        xPortion->setPropertyValues( aNames, aValues );
    }
    catch (const UnknownPropertyException&)
    {
        // error handling through return code!
        bRet = false;
    }

    return bRet;
}

sal_Bool SwAccessibleParagraph::setText( const OUString& sText )
{
    return replaceText(0, GetString().getLength(), sText);
}

// XAccessibleSelection

void SwAccessibleParagraph::selectAccessibleChild(
    sal_Int64 nChildIndex )
{
    ThrowIfDisposed();

    m_aSelectionHelper.selectAccessibleChild(nChildIndex);
}

sal_Bool SwAccessibleParagraph::isAccessibleChildSelected(
    sal_Int64 nChildIndex )
{
    ThrowIfDisposed();

    return m_aSelectionHelper.isAccessibleChildSelected(nChildIndex);
}

void SwAccessibleParagraph::clearAccessibleSelection(  )
{
    ThrowIfDisposed();
}

void SwAccessibleParagraph::selectAllAccessibleChildren(  )
{
    ThrowIfDisposed();

    m_aSelectionHelper.selectAllAccessibleChildren();
}

sal_Int64 SwAccessibleParagraph::getSelectedAccessibleChildCount(  )
{
    ThrowIfDisposed();

    return m_aSelectionHelper.getSelectedAccessibleChildCount();
}

uno::Reference<XAccessible> SwAccessibleParagraph::getSelectedAccessibleChild(
    sal_Int64 nSelectedChildIndex )
{
    ThrowIfDisposed();

    return m_aSelectionHelper.getSelectedAccessibleChild(nSelectedChildIndex);
}

// index has to be treated as global child index.
void SwAccessibleParagraph::deselectAccessibleChild(
    sal_Int64 nChildIndex )
{
    ThrowIfDisposed();

    m_aSelectionHelper.deselectAccessibleChild( nChildIndex );
}

// XAccessibleHypertext

namespace {

class SwHyperlinkIter_Impl
{
    SwTextFrame const& m_rFrame;
    sw::MergedAttrIter m_Iter;
    TextFrameIndex m_nStart;
    TextFrameIndex m_nEnd;

public:
    explicit SwHyperlinkIter_Impl(const SwTextFrame & rTextFrame);
    const SwTextAttr *next(SwTextNode const** ppNode = nullptr);

    TextFrameIndex startIdx() const { return m_nStart; }
    TextFrameIndex endIdx() const { return m_nEnd; }
};

}

SwHyperlinkIter_Impl::SwHyperlinkIter_Impl(const SwTextFrame & rTextFrame)
    : m_rFrame(rTextFrame)
    , m_Iter(rTextFrame)
    , m_nStart(rTextFrame.GetOffset())
{
    const SwTextFrame *const pFollFrame = rTextFrame.GetFollow();
    m_nEnd = pFollFrame ? pFollFrame->GetOffset() : TextFrameIndex(rTextFrame.GetText().getLength());
}

const SwTextAttr *SwHyperlinkIter_Impl::next(SwTextNode const** ppNode)
{
    const SwTextAttr *pAttr = nullptr;
    if (ppNode)
    {
        *ppNode = nullptr;
    }

    SwTextNode const* pNode(nullptr);
    while (SwTextAttr const*const pHt = m_Iter.NextAttr(&pNode))
    {
        if (RES_TXTATR_INETFMT == pHt->Which())
        {
            const TextFrameIndex nHtStart(m_rFrame.MapModelToView(pNode, pHt->GetStart()));
            const TextFrameIndex nHtEnd(m_rFrame.MapModelToView(pNode, pHt->GetAnyEnd()));
            if (nHtEnd > nHtStart &&
                ((nHtStart >= m_nStart && nHtStart < m_nEnd) ||
                 (nHtEnd > m_nStart && nHtEnd <= m_nEnd)))
            {
                pAttr = pHt;
                if (ppNode)
                {
                    *ppNode = pNode;
                }
                break;
            }
        }
    }

    return pAttr;
};

sal_Int32 SAL_CALL SwAccessibleParagraph::getHyperLinkCount()
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    sal_Int32 nCount = 0;
    // #i77108# - provide hyperlinks also in editable documents.

    const SwTextFrame* pTextFrame = GetTextFrame();
    SwHyperlinkIter_Impl aIter(*pTextFrame);
    while( aIter.next() )
        nCount++;

    return nCount;
}

uno::Reference< XAccessibleHyperlink > SAL_CALL
    SwAccessibleParagraph::getHyperLink( sal_Int32 nLinkIndex )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    const SwTextFrame* pTextFrame = GetTextFrame();
    SwHyperlinkIter_Impl aHIter(*pTextFrame);
    SwTextNode const* pNode(nullptr);
    const SwTextAttr* pHt = aHIter.next(&pNode);
    for (sal_Int32 nTIndex = 0; pHt && nTIndex < nLinkIndex; ++nTIndex)
        pHt = aHIter.next(&pNode);

    if (!pHt)
        throw lang::IndexOutOfBoundsException();

    rtl::Reference<SwAccessibleHyperlink> xRet;
    if (!m_pHyperTextData)
        m_pHyperTextData.reset( new SwAccessibleHyperTextData );
    SwAccessibleHyperTextData::iterator aIter = m_pHyperTextData->find(pHt);
    if (aIter != m_pHyperTextData->end())
    {
        xRet = (*aIter).second;
    }
    if (!xRet.is())
    {
        TextFrameIndex const nHintStart(pTextFrame->MapModelToView(pNode, pHt->GetStart()));
        TextFrameIndex const nHintEnd(pTextFrame->MapModelToView(pNode, pHt->GetAnyEnd()));
        const sal_Int32 nTmpHStt = GetPortionData().GetAccessiblePosition(
            std::max(aHIter.startIdx(), nHintStart));
        const sal_Int32 nTmpHEnd = GetPortionData().GetAccessiblePosition(
            std::min(aHIter.endIdx(), nHintEnd));
        xRet = new SwAccessibleHyperlink(*pHt,
                                         *this, nTmpHStt, nTmpHEnd );
        if (aIter != m_pHyperTextData->end())
        {
            (*aIter).second = xRet.get();
        }
        else
        {
            m_pHyperTextData->emplace( pHt, xRet );
        }
    }
    return xRet;
}

sal_Int32 SAL_CALL SwAccessibleParagraph::getHyperLinkIndex( sal_Int32 nCharIndex )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    // parameter checking
    sal_Int32 nLength = GetString().getLength();
    if ( ! IsValidPosition( nCharIndex, nLength ) )
    {
        throw lang::IndexOutOfBoundsException();
    }

    sal_Int32 nRet = -1;
    // #i77108#
    {
        const SwTextFrame* pTextFrame = GetTextFrame();
        SwHyperlinkIter_Impl aHIter(*pTextFrame);

        const TextFrameIndex nIdx = GetPortionData().GetCoreViewPosition(nCharIndex);
        sal_Int32 nPos = 0;
        SwTextNode const* pNode(nullptr);
        const SwTextAttr *pHt = aHIter.next(&pNode);
        while (pHt && (nIdx < pTextFrame->MapModelToView(pNode, pHt->GetStart())
                       || nIdx >= pTextFrame->MapModelToView(pNode, pHt->GetAnyEnd())))
        {
            pHt = aHIter.next(&pNode);
            nPos++;
        }

        if( pHt )
            nRet = nPos;
    }

    if (nRet == -1)
        throw lang::IndexOutOfBoundsException();
    return nRet;
}

// #i71360#, #i108125# - adjustments for change tracking text markup
sal_Int32 SAL_CALL SwAccessibleParagraph::getTextMarkupCount( sal_Int32 nTextMarkupType )
{
    SolarMutexGuard g;

    std::unique_ptr<SwTextMarkupHelper> pTextMarkupHelper;
    switch ( nTextMarkupType )
    {
        case text::TextMarkupType::TRACK_CHANGE_INSERTION:
        case text::TextMarkupType::TRACK_CHANGE_DELETION:
        case text::TextMarkupType::TRACK_CHANGE_FORMATCHANGE:
        {
            pTextMarkupHelper.reset( new SwTextMarkupHelper(
                GetPortionData(),
                *(mpParaChangeTrackInfo->getChangeTrackingTextMarkupList( nTextMarkupType ) )) );
        }
        break;
        default:
        {
            const SwTextFrame* const pFrame = GetTextFrame();
            pTextMarkupHelper.reset(new SwTextMarkupHelper(GetPortionData(), *pFrame));
        }
    }

    return pTextMarkupHelper->getTextMarkupCount( nTextMarkupType );
}

//MSAA Extension Implementation in app  module
sal_Bool SAL_CALL SwAccessibleParagraph::scrollToPosition( const css::awt::Point&, sal_Bool )
{
    return false;
}

sal_Int32 SAL_CALL SwAccessibleParagraph::getSelectedPortionCount(  )
{
    SolarMutexGuard g;

    sal_Int32 nSelected = 0;
    SwPaM* pCursor = GetCursor( true );
    if( pCursor != nullptr )
    {
        // get SwPosition for my node
        const SwTextFrame* const pFrame = GetTextFrame();
        SwNodeOffset nFirstNode(pFrame->GetTextNodeFirst()->GetIndex());
        SwNodeOffset nLastNode;
        if (sw::MergedPara const*const pMerged = pFrame->GetMergedPara())
        {
            nLastNode = pMerged->pLastNode->GetIndex();
        }
        else
        {
            nLastNode = nFirstNode;
        }

        // iterate over ring
        for(SwPaM& rTmpCursor : pCursor->GetRingContainer())
        {
            // ignore, if no mark
            if( rTmpCursor.HasMark() )
            {
                // check whether frame's node(s) are 'inside' pCursor
                SwPosition* pStart = rTmpCursor.Start();
                SwNodeOffset nStartIndex = pStart->GetNodeIndex();
                SwPosition* pEnd = rTmpCursor.End();
                SwNodeOffset nEndIndex = pEnd->GetNodeIndex();
                if ((nStartIndex <= nLastNode) && (nFirstNode <= nEndIndex))
                {
                    nSelected++;
                }
                // else: this PaM doesn't point to this paragraph
            }
            // else: this PaM is collapsed and doesn't select anything
        }
    }
    return nSelected;

}

sal_Int32 SAL_CALL SwAccessibleParagraph::getSeletedPositionStart( sal_Int32 nSelectedPortionIndex )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    sal_Int32 nStart=-1, nEnd=-1;
    /*sal_Bool bSelected = */GetSelectionAtIndex(&nSelectedPortionIndex, nStart, nEnd );
    return nStart;
}

sal_Int32 SAL_CALL SwAccessibleParagraph::getSeletedPositionEnd( sal_Int32 nSelectedPortionIndex )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    sal_Int32 nStart=-1, nEnd=-1;
    /*sal_Bool bSelected = */GetSelectionAtIndex(&nSelectedPortionIndex, nStart, nEnd );
    return nEnd;
}

sal_Bool SAL_CALL SwAccessibleParagraph::removeSelection( sal_Int32 selectionIndex )
{
    SolarMutexGuard g;

    if(selectionIndex < 0) return false;

    sal_Int32 nSelected = selectionIndex;

    // get the selection, and test whether it affects our text node
    SwPaM* pCursor = GetCursor( true );

    if( pCursor != nullptr )
    {
        bool bRet = false;

        // get SwPosition for my node
        const SwTextFrame* const pFrame = GetTextFrame();
        SwNodeOffset nFirstNode(pFrame->GetTextNodeFirst()->GetIndex());
        SwNodeOffset nLastNode;
        if (sw::MergedPara const*const pMerged = pFrame->GetMergedPara())
        {
            nLastNode = pMerged->pLastNode->GetIndex();
        }
        else
        {
            nLastNode = nFirstNode;
        }

        // iterate over ring
        SwPaM* pRingStart = pCursor;
        do
        {
            // ignore, if no mark
            if( pCursor->HasMark() )
            {
                // check whether frame's node(s) are 'inside' pCursor
                SwPosition* pStart = pCursor->Start();
                SwNodeOffset nStartIndex = pStart->GetNodeIndex();
                SwPosition* pEnd = pCursor->End();
                SwNodeOffset nEndIndex = pEnd->GetNodeIndex();
                if ((nStartIndex <= nLastNode) && (nFirstNode <= nEndIndex))
                {
                    if( nSelected == 0 )
                    {
                        pCursor->MoveTo(nullptr);
                        delete pCursor;
                        bRet = true;
                    }
                    else
                    {
                        nSelected--;
                    }
                }
            }
            // else: this PaM is collapsed and doesn't select anything
            if(!bRet)
                pCursor = pCursor->GetNext();
        }
        while( !bRet && (pCursor != pRingStart) );
    }
    return true;
}

sal_Int32 SAL_CALL SwAccessibleParagraph::addSelection( sal_Int32, sal_Int32 startOffset, sal_Int32 endOffset)
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    // parameter checking
    sal_Int32 nLength = GetString().getLength();
    if ( ! IsValidRange( startOffset, endOffset, nLength ) )
    {
        throw lang::IndexOutOfBoundsException();
    }

    sal_Int32 nSelectedCount = getSelectedPortionCount();
    for ( sal_Int32 i = nSelectedCount ; i >= 0 ; i--)
    {
        sal_Int32 nStart, nEnd;
        bool bSelected = GetSelectionAtIndex(&i, nStart, nEnd );
        if(bSelected)
        {
            if(nStart <= nEnd )
            {
                if (( startOffset>=nStart && startOffset <=nEnd ) ||     //startOffset in a selection
                       ( endOffset>=nStart && endOffset <=nEnd )     ||  //endOffset in a selection
                    ( startOffset <= nStart && endOffset >=nEnd)  ||       //start and  end include the old selection
                    ( startOffset >= nStart && endOffset <=nEnd) )
                {
                    removeSelection(i);
                }

            }
            else
            {
                if (( startOffset>=nEnd && startOffset <=nStart ) ||     //startOffset in a selection
                       ( endOffset>=nEnd && endOffset <=nStart )     || //endOffset in a selection
                    ( startOffset <= nStart && endOffset >=nEnd)  ||       //start and  end include the old selection
                    ( startOffset >= nStart && endOffset <=nEnd) )

                {
                    removeSelection(i);
                }
            }
        }

    }

    // get cursor shell
    SwCursorShell* pCursorShell = GetCursorShell();
    if( pCursorShell != nullptr )
    {
        // create pam for selection
        pCursorShell->StartAction();
        const SwTextFrame* const pFrame = GetTextFrame();
        SwPaM* aPaM = pCursorShell->CreateCursor();
        aPaM->SetMark();
        *aPaM->GetPoint() = pFrame->MapViewToModelPos(GetPortionData().GetCoreViewPosition(startOffset));
        *aPaM->GetMark() = pFrame->MapViewToModelPos(GetPortionData().GetCoreViewPosition(endOffset));
        pCursorShell->EndAction();
    }

    return 0;
}

TextSegment SAL_CALL SwAccessibleParagraph::getTextMarkup(sal_Int32 nTextMarkupIndex,
                                                          sal_Int32 nTextMarkupType)
{
    SolarMutexGuard g;

    std::unique_ptr<SwTextMarkupHelper> pTextMarkupHelper;
    switch ( nTextMarkupType )
    {
        case text::TextMarkupType::TRACK_CHANGE_INSERTION:
        case text::TextMarkupType::TRACK_CHANGE_DELETION:
        case text::TextMarkupType::TRACK_CHANGE_FORMATCHANGE:
        {
            pTextMarkupHelper.reset( new SwTextMarkupHelper(
                GetPortionData(),
                *(mpParaChangeTrackInfo->getChangeTrackingTextMarkupList( nTextMarkupType ) )) );
        }
        break;
        default:
        {
            const SwTextFrame* const pFrame = GetTextFrame();
            pTextMarkupHelper.reset(new SwTextMarkupHelper(GetPortionData(), *pFrame));
        }
    }

    return pTextMarkupHelper->getTextMarkup( nTextMarkupIndex, nTextMarkupType );
}

uno::Sequence<TextSegment> SAL_CALL
SwAccessibleParagraph::getTextMarkupAtIndex(sal_Int32 nCharIndex, sal_Int32 nTextMarkupType)
{
    SolarMutexGuard g;

    // parameter checking
    const sal_Int32 nLength = GetString().getLength();
    if ( ! IsValidPosition( nCharIndex, nLength ) )
    {
        throw lang::IndexOutOfBoundsException();
    }

    std::unique_ptr<SwTextMarkupHelper> pTextMarkupHelper;
    switch ( nTextMarkupType )
    {
        case text::TextMarkupType::TRACK_CHANGE_INSERTION:
        case text::TextMarkupType::TRACK_CHANGE_DELETION:
        case text::TextMarkupType::TRACK_CHANGE_FORMATCHANGE:
        {
            pTextMarkupHelper.reset( new SwTextMarkupHelper(
                GetPortionData(),
                *(mpParaChangeTrackInfo->getChangeTrackingTextMarkupList( nTextMarkupType ) )) );
        }
        break;
        default:
        {
            const SwTextFrame* const pFrame = GetTextFrame();
            pTextMarkupHelper.reset(new SwTextMarkupHelper(GetPortionData(), *pFrame));
        }
    }

    return pTextMarkupHelper->getTextMarkupAtIndex( nCharIndex, nTextMarkupType );
}

// #i89175#
sal_Int32 SAL_CALL SwAccessibleParagraph::getLineNumberAtIndex( sal_Int32 nIndex )
{
    SolarMutexGuard g;

    // parameter checking
    const sal_Int32 nLength = GetString().getLength();
    if ( ! IsValidPosition( nIndex, nLength ) )
    {
        throw lang::IndexOutOfBoundsException();
    }

    const sal_Int32 nLineNo = GetPortionData().GetLineNo( nIndex );
    return nLineNo;
}

TextSegment SAL_CALL SwAccessibleParagraph::getTextAtLineNumber(sal_Int32 nLineNo)
{
    SolarMutexGuard g;

    // parameter checking
    if ( nLineNo < 0 ||
         nLineNo >= GetPortionData().GetLineCount() )
    {
        throw lang::IndexOutOfBoundsException();
    }

    i18n::Boundary aLineBound;
    GetPortionData().GetBoundaryOfLine( nLineNo, aLineBound );

    TextSegment aTextAtLine;
    const OUString rText = GetString();
    aTextAtLine.SegmentText = rText.copy( aLineBound.startPos,
                                          aLineBound.endPos - aLineBound.startPos );
    aTextAtLine.SegmentStart = aLineBound.startPos;
    aTextAtLine.SegmentEnd = aLineBound.endPos;

    return aTextAtLine;
}

TextSegment SAL_CALL SwAccessibleParagraph::getTextAtLineWithCaret()
{
    SolarMutexGuard g;

    const sal_Int32 nLineNoOfCaret = getNumberOfLineWithCaret();

    if ( nLineNoOfCaret >= 0 &&
         nLineNoOfCaret < GetPortionData().GetLineCount() )
    {
        return getTextAtLineNumber( nLineNoOfCaret );
    }

    return TextSegment();
}

sal_Int32 SAL_CALL SwAccessibleParagraph::getNumberOfLineWithCaret()
{
    SolarMutexGuard g;

    const sal_Int32 nCaretPos = getCaretPosition();
    const sal_Int32 nLength = GetString().getLength();
    if ( !IsValidPosition( nCaretPos, nLength ) )
    {
        return -1;
    }

    sal_Int32 nLineNo = GetPortionData().GetLineNo( nCaretPos );

    // special handling for cursor positioned at end of text line via End key
    if ( nCaretPos != 0 )
    {
        i18n::Boundary aLineBound;
        GetPortionData().GetBoundaryOfLine( nLineNo, aLineBound );
        if ( nCaretPos == aLineBound.startPos )
        {
            SwCursorShell* pCursorShell = SwAccessibleParagraph::GetCursorShell();
            if ( pCursorShell != nullptr )
            {
                const awt::Rectangle aCharRect = getCharacterBounds( nCaretPos );

                const SwRect& aCursorCoreRect = pCursorShell->GetCharRect();
                // translate core coordinates into accessibility coordinates
                vcl::Window *pWin = GetWindow();
                if (!pWin)
                {
                    throw uno::RuntimeException(u"no Window"_ustr, getXWeak());
                }

                tools::Rectangle aScreenRect( GetMap()->CoreToPixel( aCursorCoreRect ));

                SwRect aFrameLogBounds( GetBounds( *(GetMap()) ) ); // twip rel to doc root
                Point aFramePixPos( GetMap()->CoreToPixel( aFrameLogBounds ).TopLeft() );
                aScreenRect.Move( -aFramePixPos.getX(), -aFramePixPos.getY() );

                // convert into AWT Rectangle
                const awt::Rectangle aCursorRect( aScreenRect.Left(),
                                                  aScreenRect.Top(),
                                                  aScreenRect.GetWidth(),
                                                  aScreenRect.GetHeight() );

                if ( aCharRect.X != aCursorRect.X ||
                     aCharRect.Y != aCursorRect.Y )
                {
                    --nLineNo;
                }
            }
        }
    }

    return nLineNo;
}

// #i108125#
void SwAccessibleParagraph::Notify(SfxBroadcaster&, const SfxHint&)
{
    mpParaChangeTrackInfo->reset();
}

bool SwAccessibleParagraph::GetSelectionAtIndex(
    sal_Int32 * pSelection, sal_Int32& nStart, sal_Int32& nEnd)
{
    if (pSelection && *pSelection < 0) return false;

    bool bRet = false;
    nStart = -1;
    nEnd = -1;

    // get the selection, and test whether it affects our text node
    SwPaM* pCursor = GetCursor( true );
    if( pCursor != nullptr )
    {
        // get SwPosition for my node
        const SwTextFrame* const pFrame = GetTextFrame();
        SwNodeOffset nFirstNode(pFrame->GetTextNodeFirst()->GetIndex());
        SwNodeOffset nLastNode;
        if (sw::MergedPara const*const pMerged = pFrame->GetMergedPara())
        {
            nLastNode = pMerged->pLastNode->GetIndex();
        }
        else
        {
            nLastNode = nFirstNode;
        }

        // iterate over ring
        for(SwPaM& rTmpCursor : pCursor->GetRingContainer())
        {
            // ignore, if no mark
            if( rTmpCursor.HasMark() )
            {
                // check whether frame's node(s) are 'inside' pCursor
                SwPosition* pStart = rTmpCursor.Start();
                SwNodeOffset nStartIndex = pStart->GetNodeIndex();
                SwPosition* pEnd = rTmpCursor.End();
                SwNodeOffset nEndIndex = pEnd->GetNodeIndex();
                if ((nStartIndex <= nLastNode) && (nFirstNode <= nEndIndex))
                {
                    if (!pSelection || *pSelection == 0)
                    {
                        // translate start and end positions

                        // start position
                        sal_Int32 nLocalStart = -1;
                        if (nStartIndex < nFirstNode)
                        {
                            // selection starts in previous node:
                            // then our local selection starts with the paragraph
                            nLocalStart = 0;
                        }
                        else
                        {
                            assert(FrameContainsNode(*pFrame, nStartIndex));

                            // selection starts in this node:
                            // then check whether it's before or inside our part of
                            // the paragraph, and if so, get the proper position
                            const TextFrameIndex nCoreStart =
                                pFrame->MapModelToViewPos(*pStart);
                            if( nCoreStart <
                                GetPortionData().GetFirstValidCorePosition() )
                            {
                                nLocalStart = 0;
                            }
                            else if( nCoreStart <=
                                     GetPortionData().GetLastValidCorePosition() )
                            {
                                SAL_WARN_IF(
                                    !GetPortionData().IsValidCorePosition(
                                                                  nCoreStart),
                                    "sw.a11y",
                                    "problem determining valid core position");

                                nLocalStart =
                                    GetPortionData().GetAccessiblePosition(
                                                                      nCoreStart );
                            }
                        }

                        // end position
                        sal_Int32 nLocalEnd = -1;
                        if (nLastNode < nEndIndex)
                        {
                            // selection ends in following node:
                            // then our local selection extends to the end
                            nLocalEnd = GetPortionData().GetAccessibleString().
                                                                       getLength();
                        }
                        else
                        {
                            assert(FrameContainsNode(*pFrame, nEndIndex));

                            // selection ends in this node: then select everything
                            // before our part of the node
                            const TextFrameIndex nCoreEnd =
                                pFrame->MapModelToViewPos(*pEnd);
                            if( nCoreEnd >
                                    GetPortionData().GetLastValidCorePosition() )
                            {
                                // selection extends beyond out part of this para
                                nLocalEnd = GetPortionData().GetAccessibleString().
                                                                       getLength();
                            }
                            else if( nCoreEnd >=
                                     GetPortionData().GetFirstValidCorePosition() )
                            {
                                // selection is inside our part of this para
                                SAL_WARN_IF(
                                    !GetPortionData().IsValidCorePosition(
                                                                  nCoreEnd),
                                    "sw.a11y",
                                    "problem determining valid core position");

                                nLocalEnd = GetPortionData().GetAccessiblePosition(
                                                                       nCoreEnd );
                            }
                        }

                        if( ( nLocalStart != -1 ) && ( nLocalEnd != -1 ) )
                        {
                            nStart = nLocalStart;
                            nEnd = nLocalEnd;
                            bRet = true;
                        }
                    } // if hit the index
                    else
                    {
                        --*pSelection;
                    }
                }
                // else: this PaM doesn't point to this paragraph
            }
            // else: this PaM is collapsed and doesn't select anything
            if(bRet)
                break;
        }
    }
    // else: nocursor -> no selection

    if (pSelection && bRet)
    {
        sal_Int32 nCaretPos = GetCaretPos();
        if( nStart == nCaretPos )
            std::swap( nStart, nEnd );
    }
    return bRet;
}

sal_Int16 SAL_CALL SwAccessibleParagraph::getAccessibleRole()
{
    std::scoped_lock aGuard( m_Mutex );

    //Get the real heading level, Heading1 ~ Heading10
    if (m_nHeadingLevel > 0)
        return AccessibleRole::HEADING;
    if (m_bIsBlockQuote)
        return AccessibleRole::BLOCK_QUOTE;
    else
        return AccessibleRole::PARAGRAPH;
}

//Get the real heading level, Heading1 ~ Heading10
sal_Int32 SwAccessibleParagraph::GetRealHeadingLevel()
{
    rtl::Reference< SwXTextPortion > xPortion = CreateUnoPortion( 0, 0 );
    uno::Any styleAny = xPortion->getPropertyValue( u"ParaStyleName"_ustr );
    OUString sValue;
    if (styleAny >>= sValue)
    {
        sal_Int32 length = sValue.getLength();
        if (length == 9 || length == 10)
        {
            if (sValue.startsWith("Heading"))
            {
                std::u16string_view intStr = sValue.subView(8);
                sal_Int32 headingLevel = o3tl::toInt32(intStr);
                return headingLevel;
            }
        }
    }
    return -1;
}

bool SwAccessibleParagraph::IsBlockQuote()
{
    rtl::Reference<SwXTextPortion> xPortion = CreateUnoPortion(0, 0);
    uno::Any aStyleAny = xPortion->getPropertyValue(u"ParaStyleName"_ustr);
    OUString sValue;
    if (aStyleAny >>= sValue)
        return sValue == "Quotations";
    return false;
}

OUString SAL_CALL SwAccessibleParagraph::getExtendedAttributes()
{
    SolarMutexGuard g;

    OUString strHeading;
    if (m_nHeadingLevel >= 0)
    {
        // report heading level using the "level" object attribute as specified in ARIA,
        // maps to attributes of the same name for AT-SPI, IAccessible2, UIA
        // https://www.w3.org/TR/core-aam-1.2/#ariaLevelHeading
        strHeading = "level:" + OUString::number(m_nHeadingLevel) + ";";
    }

    return strHeading;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
