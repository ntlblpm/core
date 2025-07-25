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

#include <vcl/svapp.hxx>
#include <vcl/window.hxx>
#include <editeng/lspcitem.hxx>
#include <editeng/flditem.hxx>
#include "impedit.hxx"
#include <editeng/editeng.hxx>
#include <editeng/editview.hxx>
#include <eerdll2.hxx>
#include <editeng/eerdll.hxx>
#include <edtspell.hxx>
#include "eeobj.hxx"
#include <editeng/txtrange.hxx>
#include <sfx2/app.hxx>
#include <sfx2/mieclip.hxx>
#include <sfx2/viewsh.hxx>
#include <svtools/colorcfg.hxx>
#include <svl/ctloptions.hxx>
#include <unotools/securityoptions.hxx>
#include <editeng/acorrcfg.hxx>
#include <editeng/lrspitem.hxx>
#include <editeng/ulspitem.hxx>
#include <editeng/adjustitem.hxx>
#include <editeng/frmdiritem.hxx>
#include <editeng/justifyitem.hxx>
#include <editeng/udlnitem.hxx>
#include <editeng/scripthintitem.hxx>

#include <com/sun/star/i18n/CharacterIteratorMode.hpp>
#include <com/sun/star/i18n/WordType.hpp>
#include <com/sun/star/i18n/ScriptType.hpp>
#include <com/sun/star/lang/Locale.hpp>
#include <com/sun/star/i18n/InputSequenceCheckMode.hpp>
#include <com/sun/star/system/SystemShellExecute.hpp>
#include <com/sun/star/system/SystemShellExecuteFlags.hpp>
#include <com/sun/star/system/XSystemShellExecute.hpp>
#include <com/sun/star/i18n/UnicodeType.hpp>

#include <rtl/character.hxx>

#include <sal/log.hxx>
#include <o3tl/safeint.hxx>
#include <osl/diagnose.h>
#include <sot/exchange.hxx>
#include <sot/formats.hxx>
#include <svl/asiancfg.hxx>
#include <svl/voiditem.hxx>
#include <i18nutil/unicode.hxx>
#include <i18nutil/scriptchangescanner.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <comphelper/flagguard.hxx>
#include <comphelper/lok.hxx>
#include <comphelper/processfactory.hxx>
#include <comphelper/configuration.hxx>

#include <unicode/ubidi.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <string_view>
#include <fstream>
#include <outleeng.hxx>

using namespace ::com::sun::star;

static sal_uInt16 lcl_CalcExtraSpace( const SvxLineSpacingItem& rLSItem )
{
    sal_uInt16 nExtra = 0;
    if ( rLSItem.GetInterLineSpaceRule() == SvxInterLineSpaceRule::Fix )
    {
        nExtra = rLSItem.GetInterLineSpace();
    }

    return nExtra;
}

constexpr tools::Long constMaxPaperSize = 0x7FFFFFFF;

ImpEditEngine::ImpEditEngine( EditEngine* pEE, SfxItemPool* pItemPool ) :
    pSharedVCL(EditDLL::Get().GetSharedVclResources()),
    maPaperSize(constMaxPaperSize, constMaxPaperSize),
    maMinAutoPaperSize(0, 0),
    maMaxAutoPaperSize(constMaxPaperSize, constMaxPaperSize),
    maEditDoc( pItemPool ),
    mpEditEngine(pEE),
    mpActiveView(nullptr),
    mpStylePool(nullptr),
    mpUndoManager(nullptr),
    maWordDelimiters(u" .,;:-`'?!_=\"{}()[]"_ustr),
    maBackgroundColor(COL_AUTO),
    mbRoundToNearestPt(false),
    mnAsianCompressionMode(CharCompressType::NONE),
    meDefaultHorizontalTextDirection(EEHorizontalTextDirection::Default),
    mnBigTextObjectStart(20),
    meDefLanguage(LANGUAGE_DONTKNOW),
    mnCurTextHeight(0),
    maOnlineSpellTimer("editeng::ImpEditEngine aOnlineSpellTimer"),
    maStatusTimer("editeng::ImpEditEngine aStatusTimer"),
    mbKernAsianPunctuation(false),
    mbAddExtLeading(false),
    mbIsFormatting(false),
    mbFormatted(false),
    mbInSelection(false),
    mbIsInUndo(false),
    mbUpdateLayout(true),
    mbUndoEnabled(true),
    mbDowning(false),
    mbUseAutoColor(true),
    mbForceAutoColor(false),
    mbCallParaInsertedOrDeleted(false),
    mbFirstWordCapitalization(true),
    mbLastTryMerge(false),
    mbReplaceLeadingSingleQuotationMark(true),
    mbSkipOutsideFormat(false),
    mbFuzzing(comphelper::IsFuzzing()),
    mbNbspRunNext(false)
{
    maStatus.GetControlWord() =  EEControlBits::USECHARATTRIBS | EEControlBits::DOIDLEFORMAT |
                                EEControlBits::PASTESPECIAL | EEControlBits::UNDOATTRIBS |
                                EEControlBits::ALLOWBIGOBJS | EEControlBits::RTFSTYLESHEETS;

    maSelEngine.SetFunctionSet(&maSelFuncSet);

    maStatusTimer.SetTimeout(200);
    maStatusTimer.SetInvokeHandler(LINK(this, ImpEditEngine, StatusTimerHdl));

    maIdleFormatter.SetPriority(TaskPriority::REPAINT);
    maIdleFormatter.SetInvokeHandler(LINK(this, ImpEditEngine, IdleFormatHdl));

    maOnlineSpellTimer.SetTimeout(100);
    maOnlineSpellTimer.SetInvokeHandler(LINK( this, ImpEditEngine, OnlineSpellHdl));

    // Access data already from here on!
    SetRefDevice( nullptr );
    InitDoc( false );

    mbCallParaInsertedOrDeleted = true;

    maEditDoc.SetModifyHdl( LINK( this, ImpEditEngine, DocModified ) );
    StartListening(*SfxGetpApp());
}

void ImpEditEngine::Dispose()
{
    SolarMutexGuard g;
    auto pApp = SfxApplication::Get();
    if(pApp)
        EndListening(*pApp);
    mpVirtDev.disposeAndClear();
    mpOwnDev.disposeAndClear();
    pSharedVCL.reset();
}

ImpEditEngine::~ImpEditEngine()
{
    maStatusTimer.Stop();
    maOnlineSpellTimer.Stop();
    maIdleFormatter.Stop();

    // Destroying templates may otherwise cause unnecessary formatting,
    // when a parent template is destroyed.
    // And this after the destruction of the data!
    mbDowning = true;
    SetUpdateLayout( false );

    Dispose();
    // it's only legal to delete the mpUndoManager if it was created by
    // ImpEditEngine; if it was set by SetUndoManager() it must be cleared
    // before destroying the ImpEditEngine!
    assert(!mpUndoManager || typeid(*mpUndoManager) == typeid(EditUndoManager));
    delete mpUndoManager;
    mpTextRanger.reset();
    mpIMEInfos.reset();
    mpSpellInfo.reset();
}

void ImpEditEngine::SetRefDevice(OutputDevice* pRef)
{
    if (pRef)
        mpRefDev = pRef;
    else
        mpRefDev = pSharedVCL->GetVirtualDevice();

    mnOnePixelInRef = static_cast<sal_uInt16>(mpRefDev->PixelToLogic( Size( 1, 0 ) ).Width());

    if ( IsFormatted() )
    {
        FormatFullDoc();
        UpdateViews();
    }
}

void ImpEditEngine::SetRefMapMode( const MapMode& rMapMode )
{
    if ( GetRefDevice()->GetMapMode() == rMapMode )
        return;

    mpOwnDev.disposeAndClear();
    mpOwnDev = VclPtr<VirtualDevice>::Create();
    mpRefDev = mpOwnDev;
    mpRefDev->SetMapMode(MapMode(MapUnit::MapTwip));
    SetRefDevice(mpRefDev);

    mpRefDev->SetMapMode( rMapMode );
    mnOnePixelInRef = static_cast<sal_uInt16>(mpRefDev->PixelToLogic(Size(1, 0)).Width());
    if ( IsFormatted() )
    {
        FormatFullDoc();
        UpdateViews();
    }
}

void ImpEditEngine::InitDoc(bool bKeepParaAttribs)
{
    sal_Int32 nParas = maEditDoc.Count();
    for ( sal_Int32 n = bKeepParaAttribs ? 1 : 0; n < nParas; n++ )
    {
        if (maEditDoc.GetObject(n)->GetStyleSheet())
            EndListening( *maEditDoc.GetObject(n)->GetStyleSheet() );
    }

    if ( bKeepParaAttribs )
        maEditDoc.RemoveText();
    else
        maEditDoc.Clear();

    GetParaPortions().Reset();

    GetParaPortions().Insert(0, std::make_unique<ParaPortion>(maEditDoc.GetObject(0)));

    mbFormatted = false;

    if ( IsCallParaInsertedOrDeleted() )
    {
        GetEditEnginePtr()->ParagraphDeleted(EE_PARA_MAX);
        GetEditEnginePtr()->ParagraphInserted( 0 );
    }

    if ( GetStatus().DoOnlineSpelling() )
        maEditDoc.GetObject( 0 )->CreateWrongList();
}

EditPaM ImpEditEngine::DeleteSelected(const EditSelection& rSel)
{
    EditPaM aPaM (ImpDeleteSelection(rSel));
    return aPaM;
}

OUString ImpEditEngine::GetText( const ESelection& rESelection )
{
    return GetSelected(CreateSel(rESelection));
}

OUString ImpEditEngine::GetSelected( const EditSelection& rSel  ) const
{
    if ( !rSel.HasRange() )
        return OUString();

    EditSelection aSel( rSel );
    aSel.Adjust( maEditDoc );

    ContentNode* pStartNode = aSel.Min().GetNode();
    ContentNode* pEndNode = aSel.Max().GetNode();
    sal_Int32 nStartNode = maEditDoc.GetPos( pStartNode );
    sal_Int32 nEndNode = maEditDoc.GetPos( pEndNode );

    OSL_ENSURE( nStartNode <= nEndNode, "Selection not sorted ?" );

    // calculate buffer size we need
    sal_Int32 nBufSize = (aSel.Max().GetIndex() - aSel.Min().GetIndex() + 1)
                         + (nEndNode - nStartNode + 1);
    // protect against sometimes whacky selection
    if (nBufSize < 0 || nBufSize > 64 * 1024)
        nBufSize = 256;
    OUStringBuffer aText(nBufSize);
    const OUString aSep = EditDoc::GetSepStr( LINEEND_LF );

    // iterate over the paragraphs ...
    for ( sal_Int32 nNode = nStartNode; nNode <= nEndNode; nNode++ )
    {
        const ContentNode* pNode = maEditDoc.GetObject( nNode );
        assert(pNode);

        const sal_Int32 nStartPos = nNode==nStartNode ? aSel.Min().GetIndex() : 0;
        const sal_Int32 nEndPos = nNode==nEndNode ? aSel.Max().GetIndex() : pNode->Len(); // can also be == nStart!

        aText.append(EditDoc::GetParaAsString( pNode, nStartPos, nEndPos ));
        if ( nNode < nEndNode )
            aText.append(aSep);
    }
    return aText.makeStringAndClear();
}

bool ImpEditEngine::MouseButtonDown( const MouseEvent& rMEvt, EditView* pView )
{
    GetSelEngine().SetCurView( pView );
    SetActiveView( pView );

    if (!GetAutoCompleteText().isEmpty())
        SetAutoCompleteText( OUString(), true );

    GetSelEngine().SelMouseButtonDown( rMEvt );
    // Special treatment
    EditSelection aCurSel( pView->getImpl().GetEditSelection() );
    if ( rMEvt.IsShift() )
        return true;

    if ( rMEvt.GetClicks() == 2 )
    {
        // So that the SelectionEngine knows about the anchor.
        maSelEngine.CursorPosChanging( true, false );

        EditSelection aNewSelection( SelectWord( aCurSel ) );
        pView->getImpl().DrawSelectionXOR();
        pView->getImpl().SetEditSelection( aNewSelection );
        pView->getImpl().DrawSelectionXOR();
        pView->ShowCursor();
    }
    else if ( rMEvt.GetClicks() == 3 )
    {
        // So that the SelectionEngine knows about the anchor.
        maSelEngine.CursorPosChanging( true, false );

        EditSelection aNewSelection( aCurSel );
        aNewSelection.Min().SetIndex( 0 );
        aNewSelection.Max().SetIndex( aCurSel.Min().GetNode()->Len() );
        pView->getImpl().DrawSelectionXOR();
        pView->getImpl().SetEditSelection( aNewSelection );
        pView->getImpl().DrawSelectionXOR();
        pView->ShowCursor();
    }
    return true;
}

bool ImpEditEngine::Command( const CommandEvent& rCEvt, EditView* pView )
{
    bool bConsumed = true;

    GetSelEngine().SetCurView( pView );
    SetActiveView( pView );
    if ( rCEvt.GetCommand() == CommandEventId::StartExtTextInput )
    {
        if (!pView->IsReadOnly())
        {
            pView->DeleteSelected();
            mpIMEInfos.reset();
            EditPaM aPaM = pView->getImpl().GetEditSelection().Max();
            OUString aOldTextAfterStartPos = aPaM.GetNode()->Copy( aPaM.GetIndex() );
            sal_Int32 nMax = aOldTextAfterStartPos.indexOf( CH_FEATURE );
            if ( nMax != -1 )  // don't overwrite features!
                aOldTextAfterStartPos = aOldTextAfterStartPos.copy( 0, nMax );
            mpIMEInfos.reset( new ImplIMEInfos( aPaM, aOldTextAfterStartPos ) );
            mpIMEInfos->bWasCursorOverwrite = !pView->IsInsertMode();
            UndoActionStart( EDITUNDO_INSERT );
        }
    }
    else if ( rCEvt.GetCommand() == CommandEventId::EndExtTextInput )
    {
        if (!pView->IsReadOnly())
        {
            OSL_ENSURE( mpIMEInfos, "CommandEventId::EndExtTextInput => No start ?" );
            if( mpIMEInfos )
            {
                // #102812# convert quotes in IME text
                // works on the last input character, this is especially in Korean text often done
                // quotes that are inside of the string are not replaced!
                // Borrowed from sw: edtwin.cxx
                if ( mpIMEInfos->nLen )
                {
                    EditSelection aSel( mpIMEInfos->aPos );
                    aSel.Min().SetIndex( aSel.Min().GetIndex() + mpIMEInfos->nLen-1 );
                    aSel.Max().SetIndex( aSel.Max().GetIndex() + mpIMEInfos->nLen );
                    // #102812# convert quotes in IME text
                    // works on the last input character, this is especially in Korean text often done
                    // quotes that are inside of the string are not replaced!
                    // See also tdf#155350
                    const sal_Unicode nCharCode = aSel.Min().GetNode()->GetChar( aSel.Min().GetIndex() );
                    if ( ( GetStatus().DoAutoCorrect() ) && SvxAutoCorrect::IsAutoCorrectChar(nCharCode) )
                    {
                        aSel = DeleteSelected( aSel );
                        aSel = AutoCorrect( aSel, nCharCode, mpIMEInfos->bWasCursorOverwrite );
                        pView->getImpl().SetEditSelection( aSel );
                    }
                }

                ParaPortion* pPortion = FindParaPortion( mpIMEInfos->aPos.GetNode() );
                if (pPortion)
                    pPortion->MarkSelectionInvalid( mpIMEInfos->aPos.GetIndex() );

                bool bWasCursorOverwrite = mpIMEInfos->bWasCursorOverwrite;

                mpIMEInfos.reset();

                FormatAndLayout( pView );

                pView->SetInsertMode( !bWasCursorOverwrite );
            }
            UndoActionEnd();
        }
    }
    else if ( rCEvt.GetCommand() == CommandEventId::ExtTextInput )
    {
        if( mpIMEInfos && !pView->IsReadOnly())
        {
            OSL_ENSURE( mpIMEInfos, "CommandEventId::ExtTextInput => No Start ?" );
            const CommandExtTextInputData* pData = rCEvt.GetExtTextInputData();

            if ( !pData->IsOnlyCursorChanged() )
            {
                EditSelection aSel( mpIMEInfos->aPos );
                aSel.Max().SetIndex( aSel.Max().GetIndex() + mpIMEInfos->nLen );
                aSel = DeleteSelected( aSel );
                aSel = ImpInsertText( aSel, pData->GetText() );

                if ( mpIMEInfos->bWasCursorOverwrite )
                {
                    sal_Int32 nOldIMETextLen = mpIMEInfos->nLen;
                    sal_Int32 nNewIMETextLen = pData->GetText().getLength();

                    if ( ( nOldIMETextLen > nNewIMETextLen ) &&
                         ( nNewIMETextLen < mpIMEInfos->aOldTextAfterStartPos.getLength() ) )
                    {
                        // restore old characters
                        sal_Int32 nRestore = nOldIMETextLen - nNewIMETextLen;
                        EditPaM aPaM( mpIMEInfos->aPos );
                        aPaM.SetIndex( aPaM.GetIndex() + nNewIMETextLen );
                        ImpInsertText( EditSelection(aPaM), mpIMEInfos->aOldTextAfterStartPos.copy( nNewIMETextLen, nRestore ) );
                    }
                    else if ( ( nOldIMETextLen < nNewIMETextLen ) &&
                              ( nOldIMETextLen < mpIMEInfos->aOldTextAfterStartPos.getLength() ) )
                    {
                        // overwrite
                        sal_Int32 nOverwrite = nNewIMETextLen - nOldIMETextLen;
                        if ( ( nOldIMETextLen + nOverwrite ) > mpIMEInfos->aOldTextAfterStartPos.getLength() )
                            nOverwrite = mpIMEInfos->aOldTextAfterStartPos.getLength() - nOldIMETextLen;
                        OSL_ENSURE( nOverwrite && (nOverwrite < 0xFF00), "IME Overwrite?!" );
                        EditPaM aPaM( mpIMEInfos->aPos );
                        aPaM.SetIndex( aPaM.GetIndex() + nNewIMETextLen );
                        EditSelection _aSel( aPaM );
                        _aSel.Max().SetIndex( _aSel.Max().GetIndex() + nOverwrite );
                        DeleteSelected( _aSel );
                    }
                }
                if ( pData->GetTextAttr() )
                {
                    mpIMEInfos->CopyAttribs( pData->GetTextAttr(), pData->GetText().getLength() );
                }
                else
                {
                    mpIMEInfos->DestroyAttribs();
                    mpIMEInfos->nLen = pData->GetText().getLength();
                }

                ParaPortion* pPortion = FindParaPortion( mpIMEInfos->aPos.GetNode() );
                pPortion->MarkSelectionInvalid( mpIMEInfos->aPos.GetIndex() );
                FormatAndLayout( pView );
            }

            UpdateSelections(); // other views
            EditSelection aNewSel( EditPaM( mpIMEInfos->aPos.GetNode(), mpIMEInfos->aPos.GetIndex()+pData->GetCursorPos() ) );
            pView->SetSelection( CreateESel( aNewSel ) );
            pView->SetInsertMode( !pData->IsCursorOverwrite() );

            if ( pData->IsCursorVisible() )
                pView->ShowCursor();
            else
                pView->HideCursor();
        }
    }
    else if ( rCEvt.GetCommand() == CommandEventId::InputContextChange )
    {
    }
    else if ( rCEvt.GetCommand() == CommandEventId::CursorPos )
    {
        EditPaM aPaM( pView->getImpl().GetEditSelection().Max() );
        tools::Rectangle aR1 = PaMtoEditCursor( aPaM );

        if ( !IsFormatted() )
            FormatDoc();

        ParaPortion* pParaPortion = GetParaPortions().SafeGetObject( GetEditDoc().GetPos( aPaM.GetNode() ) );
        if (pParaPortion)
        {
            sal_Int32 nLine = pParaPortion->GetLines().FindLine( aPaM.GetIndex(), true );
            const EditLine& rLine = pParaPortion->GetLines()[nLine];

            sal_Int32 nInputEnd;
            if (mpIMEInfos)
                nInputEnd = mpIMEInfos->aPos.GetIndex() + mpIMEInfos->nLen;
            else
                nInputEnd = aPaM.GetIndex();

            if ( nInputEnd > rLine.GetEnd() )
                nInputEnd = rLine.GetEnd();
            tools::Rectangle aR2 = PaMtoEditCursor( EditPaM( aPaM.GetNode(), nInputEnd ), CursorFlags{ .bEndOfLine = true });
            tools::Rectangle aRect = pView->getImpl().GetWindowPos( aR1 );
            auto nExtTextInputWidth = aR2.Left() - aR1.Right();
            if (EditViewCallbacks* pEditViewCallbacks = pView->getEditViewCallbacks())
                pEditViewCallbacks->EditViewCursorRect(aRect, nExtTextInputWidth);
            else if (vcl::Window* pWindow = pView->GetWindow())
                pWindow->SetCursorRect(&aRect, nExtTextInputWidth);
        }
    }
    else if ( rCEvt.GetCommand() == CommandEventId::SelectionChange )
    {
        const CommandSelectionChangeData *pData = rCEvt.GetSelectionChangeData();

        ESelection aSelection = pView->GetSelection();
        aSelection.Adjust();

        if( pView->HasSelection() )
        {
            aSelection.end.nIndex = aSelection.start.nIndex;
            aSelection.start.nIndex += pData->GetStart();
            aSelection.end.nIndex += pData->GetEnd();
        }
        else
        {
            aSelection.start.nIndex = pData->GetStart();
            aSelection.end.nIndex = pData->GetEnd();
        }
        pView->SetSelection( aSelection );
    }
    else if ( rCEvt.GetCommand() == CommandEventId::PrepareReconversion )
    {
        if ( pView->HasSelection() )
        {
            ESelection aSelection = pView->GetSelection();
            aSelection.Adjust();

            if ( aSelection.start.nPara != aSelection.end.nPara )
            {
                sal_Int32 aParaLen = mpEditEngine->GetTextLen( aSelection.start.nPara );
                aSelection.end.nPara = aSelection.start.nPara;
                aSelection.end.nIndex = aParaLen;
                pView->SetSelection( aSelection );
            }
        }
    }
    else if ( rCEvt.GetCommand() == CommandEventId::QueryCharPosition )
    {
        if (mpIMEInfos)
        {
            EditPaM aPaM( pView->getImpl().GetEditSelection().Max() );
            if ( !IsFormatted() )
                FormatDoc();

            sal_Int32 nPortionPos = GetEditDoc().GetPos(aPaM.GetNode());
            ParaPortion* pParaPortion = GetParaPortions().SafeGetObject(nPortionPos);
            if (pParaPortion)
            {
                const sal_Int32 nMinPos = mpIMEInfos->aPos.GetIndex();
                const sal_Int32 nMaxPos = nMinPos + mpIMEInfos->nLen - 1;
                std::vector<tools::Rectangle> aRects(mpIMEInfos->nLen);

                auto CollectCharPositions = [&](const LineAreaInfo& rInfo) {
                    if (!rInfo.pLine) // Start of ParaPortion
                    {
                        if (rInfo.nPortion < nPortionPos)
                            return CallbackResult::SkipThisPortion;
                        if (rInfo.nPortion > nPortionPos)
                            return CallbackResult::Stop;
                        assert(&rInfo.rPortion == pParaPortion);
                    }
                    else // This is the needed ParaPortion
                    {
                        if (rInfo.pLine->GetStart() > nMaxPos)
                            return CallbackResult::Stop;
                        if (rInfo.pLine->GetEnd() < nMinPos)
                            return CallbackResult::Continue;
                        for (sal_Int32 n = nMinPos; n <= nMaxPos; ++n)
                        {
                            if (rInfo.pLine->IsIn(n))
                            {
                                tools::Rectangle aR = GetEditCursor(*pParaPortion, *rInfo.pLine, n, CursorFlags());
                                aR.Move(getTopLeftDocOffset(rInfo.aArea));
                                aRects[n - nMinPos] = pView->getImpl().GetWindowPos(aR);
                            }
                        }
                    }
                    return CallbackResult::Continue;
                };
                IterateLineAreas(CollectCharPositions, IterFlag::none);

                if (vcl::Window* pWindow = pView->GetWindow())
                    pWindow->SetCompositionCharRect(aRects.data(), aRects.size());
            }
        }
    }
    else
        bConsumed = false;

    return GetSelEngine().Command(rCEvt) || bConsumed;
}

bool ImpEditEngine::MouseButtonUp( const MouseEvent& rMEvt, EditView* pView )
{
    GetSelEngine().SetCurView( pView );
    GetSelEngine().SelMouseButtonUp( rMEvt );

    // in the tiled rendering case, setting bInSelection here has unexpected
    // consequences - further tiles painting removes the selection
    // FIXME I believe resetting bInSelection should not be here even in the
    // non-tiled-rendering case, but it has been here since 2000 (and before)
    // so who knows what corner case it was supposed to solve back then
    if (!comphelper::LibreOfficeKit::isActive())
        mbInSelection = false;

    // Special treatments
    EditSelection aCurSel( pView->getImpl().GetEditSelection() );
    if ( aCurSel.HasRange() )
        return true;

    if ( ( rMEvt.GetClicks() != 1 ) || !rMEvt.IsLeft() || rMEvt.IsMod2() )
        return true;

    const OutputDevice& rOutDev = pView->getEditViewCallbacks() ? pView->getEditViewCallbacks()->EditViewOutputDevice() : *pView->GetWindow()->GetOutDev();
    Point aLogicClick = rOutDev.PixelToLogic(rMEvt.GetPosPixel());
    const SvxFieldItem* pFld = pView->GetField(aLogicClick);
    if (!pFld)
        return true;

    // tdf#121039 When in edit mode, editeng is responsible for opening the URL on mouse click
    bool bUrlOpened = GetEditEnginePtr()->FieldClicked( *pFld );
    if (bUrlOpened)
        return true;

    if (auto pUrlField = dynamic_cast<const SvxURLField*>(pFld->GetField()))
    {
        bool bCtrlClickHappened = rMEvt.IsMod1();
        bool bCtrlClickSecOption
            = SvtSecurityOptions::IsOptionSet(SvtSecurityOptions::EOption::CtrlClickHyperlink);
        if ((bCtrlClickHappened && bCtrlClickSecOption)
            || (!bCtrlClickHappened && !bCtrlClickSecOption))
        {
            css::uno::Reference<css::system::XSystemShellExecute> exec(
                css::system::SystemShellExecute::create(
                    comphelper::getProcessComponentContext()));
            exec->execute(pUrlField->GetURL(), OUString(),
                          css::system::SystemShellExecuteFlags::DEFAULTS);
        }
    }
    return true;
}

void ImpEditEngine::ReleaseMouse()
{
    GetSelEngine().ReleaseMouse();
}

bool ImpEditEngine::MouseMove( const MouseEvent& rMEvt, EditView* pView )
{
    // MouseMove is called directly after ShowQuickHelp()!
    GetSelEngine().SetCurView( pView );
    GetSelEngine().SelMouseMove( rMEvt );
    return true;
}

EditPaM ImpEditEngine::InsertText(const EditSelection& aSel, const OUString& rStr)
{
    EditPaM aPaM = ImpInsertText( aSel, rStr );
    return aPaM;
}

void ImpEditEngine::Clear()
{
    InitDoc( false );

    EditPaM aPaM = maEditDoc.GetStartPaM();
    EditSelection aSel( aPaM );

    mnCurTextHeight = 0;

    ResetUndoManager();

    for (size_t nView = maEditViews.size(); nView; )
    {
        EditView* pView = maEditViews[--nView];
        pView->getImpl().SetEditSelection( aSel );
    }

    // Related: tdf#82115 Fix crash when handling input method events.
    // The nodes in mpIMEInfos may be deleted in ImpEditEngine::Clear() which
    // causes a crash in the CommandEventId::ExtTextInput and
    // CommandEventId::EndExtTextInput event handlers.
    mpIMEInfos.reset();
}

EditPaM ImpEditEngine::RemoveText()
{
    InitDoc( true );

    EditPaM aStartPaM = maEditDoc.GetStartPaM();
    EditSelection aEmptySel( aStartPaM, aStartPaM );
    for (EditView* pView : maEditViews)
    {
        pView->getImpl().SetEditSelection( aEmptySel );
    }
    ResetUndoManager();
    return maEditDoc.GetStartPaM();
}


void ImpEditEngine::SetText(const OUString& rText)
{
    // RemoveText deletes the undo list!
    EditPaM aStartPaM = RemoveText();
    bool bUndoCurrentlyEnabled = IsUndoEnabled();
    // The text inserted manually can not be made reversible by the user
    EnableUndo( false );

    EditSelection aEmptySel( aStartPaM, aStartPaM );
    EditPaM aPaM = aStartPaM;
    if (!rText.isEmpty())
        aPaM = ImpInsertText( aEmptySel, rText );

    for (EditView* pView : maEditViews)
    {
        pView->getImpl().SetEditSelection( EditSelection( aPaM, aPaM ) );
        //  If no text then also no Format&Update
        // => The text remains.
        if (rText.isEmpty() && IsUpdateLayout())
        {
            tools::Rectangle aTmpRect( pView->GetOutputArea().TopLeft(),
                                Size( maPaperSize.Width(), mnCurTextHeight ) );
            aTmpRect.Intersection( pView->GetOutputArea() );
            pView->InvalidateWindow( aTmpRect );
        }
    }
    if (rText.isEmpty())     // otherwise it must be invalidated later, !bFormatted is enough.
        mnCurTextHeight = 0;
    EnableUndo( bUndoCurrentlyEnabled );
    OSL_ENSURE( !HasUndoManager() || !GetUndoManager().GetUndoActionCount(), "Undo after SetText?" );
}


const SfxItemSet& ImpEditEngine::GetEmptyItemSet() const
{
    if ( !pEmptyItemSet )
    {
        pEmptyItemSet = std::make_unique<SfxItemSet>(SfxItemSet::makeFixedSfxItemSet<
                            EE_ITEMS_START, EE_ITEMS_END>(const_cast<SfxItemPool&>(maEditDoc.GetItemPool())));
        for ( sal_uInt16 nWhich = EE_ITEMS_START; nWhich <= EE_CHAR_END; nWhich++)
        {
            pEmptyItemSet->ClearItem( nWhich );
        }
    }
    return *pEmptyItemSet;
}


//  MISC

void ImpEditEngine::TextModified()
{
    mbFormatted = false;

    if ( GetNotifyHdl().IsSet() )
    {
        EENotify aNotify( EE_NOTIFY_TEXTMODIFIED );
        GetNotifyHdl().Call( aNotify );
    }
}


void ImpEditEngine::ParaAttribsChanged( ContentNode const * pNode, bool bIgnoreUndoCheck )
{
    assert(pNode && "ParaAttribsChanged: Which one?");

    maEditDoc.SetModified( true );
    mbFormatted = false;

    ParaPortion* pPortion = FindParaPortion( pNode );
    assert(pPortion);
    pPortion->MarkSelectionInvalid( 0 );

    sal_Int32 nPara = maEditDoc.GetPos( pNode );
    assert(nPara != EE_PARA_MAX);
    if (bIgnoreUndoCheck || mpEditEngine->IsInUndo())
        mpEditEngine->ParaAttribsChanged( nPara );

    ParaPortion* pNextPortion = GetParaPortions().SafeGetObject( nPara+1 );
    // => is formatted again anyway, if Invalid.
    if ( pNextPortion && !pNextPortion->IsInvalid() )
        CalcHeight(*pNextPortion);
}


//  Cursor movements

EditSelection const& ImpEditEngine::MoveCursor(const KeyEvent& rKeyEvent, EditView* pEditView,
                                               CursorFlags* pOutCursorFlags)
{
    // Actually, only necessary for up/down, but whatever.
    CheckIdleFormatter();

    EditPaM aPaM(pEditView->getImpl().GetEditSelection().Max());

    EditPaM aOldPaM( aPaM );

    // tdf#151336: Moving the cursor may advance to another paragraph. When that happens,
    // special handling is needed for correct visual cursor placement in RTL text.
    auto fnSetStartOfLineFlag = [&]
    {
        if (pOutCursorFlags)
        {
            pOutCursorFlags->bStartOfLine = true;
        }
    };

    auto fnSetParaChangeStartOfLineFlag = [&]
    {
        if (pOutCursorFlags && aPaM.GetNode() != aOldPaM.GetNode())
        {
            pOutCursorFlags->bStartOfLine = true;
        }
    };

    auto fnSetEndOfLineFlag = [&]
    {
        if (pOutCursorFlags)
        {
            pOutCursorFlags->bEndOfLine = true;
        }
    };

    auto fnSetParaChangeEndOfLineFlag = [&]
    {
        if (pOutCursorFlags && aPaM.GetNode() != aOldPaM.GetNode())
        {
            pOutCursorFlags->bEndOfLine = true;
        }
    };

    TextDirectionality eTextDirection = TextDirectionality::LeftToRight_TopToBottom;
    if (IsEffectivelyVertical() && IsTopToBottom())
        eTextDirection = TextDirectionality::TopToBottom_RightToLeft;
    else if (IsEffectivelyVertical() && !IsTopToBottom())
        eTextDirection = TextDirectionality::BottomToTop_LeftToRight;
    else if ( IsRightToLeft( GetEditDoc().GetPos( aPaM.GetNode() ) ) )
        eTextDirection = TextDirectionality::RightToLeft_TopToBottom;

    KeyEvent aTranslatedKeyEvent = rKeyEvent.LogicalTextDirectionality( eTextDirection );

    bool bCtrl = aTranslatedKeyEvent.GetKeyCode().IsMod1();
    sal_uInt16 nCode = aTranslatedKeyEvent.GetKeyCode().GetCode();

    if ( DoVisualCursorTraveling() )
    {
        // Only for simple cursor movement...
        if ( !bCtrl && ( ( nCode == KEY_LEFT ) || ( nCode == KEY_RIGHT ) ) )
        {
            aPaM = CursorVisualLeftRight( pEditView, aPaM, rKeyEvent.GetKeyCode().IsMod2() ? i18n::CharacterIteratorMode::SKIPCHARACTER : i18n::CharacterIteratorMode::SKIPCELL, rKeyEvent.GetKeyCode().GetCode() == KEY_LEFT );
            nCode = 0;  // skip switch statement
        }
    }

    bool bKeyModifySelection = aTranslatedKeyEvent.GetKeyCode().IsShift();
    switch ( nCode )
    {
        case KEY_UP:        aPaM = CursorUp( aPaM, pEditView );
                            break;
        case KEY_DOWN:      aPaM = CursorDown( aPaM, pEditView );
                            break;
        case KEY_LEFT:      aPaM = bCtrl ? WordLeft( aPaM ) : CursorLeft( aPaM, aTranslatedKeyEvent.GetKeyCode().IsMod2() ? i18n::CharacterIteratorMode::SKIPCHARACTER : i18n::CharacterIteratorMode::SKIPCELL );
                            fnSetParaChangeEndOfLineFlag();
                            break;
        case KEY_RIGHT:     aPaM = bCtrl ? WordRight( aPaM ) : CursorRight( aPaM, aTranslatedKeyEvent.GetKeyCode().IsMod2() ? i18n::CharacterIteratorMode::SKIPCHARACTER : i18n::CharacterIteratorMode::SKIPCELL );
                            fnSetParaChangeStartOfLineFlag();
                            break;
        case KEY_HOME:      aPaM = bCtrl ? CursorStartOfDoc() : CursorStartOfLine( aPaM );
                            fnSetStartOfLineFlag();
                            break;
        case KEY_END:       aPaM = bCtrl ? CursorEndOfDoc() : CursorEndOfLine( aPaM );
                            fnSetEndOfLineFlag();
                            break;
        case KEY_PAGEUP:    aPaM = bCtrl ? CursorStartOfDoc() : PageUp( aPaM, pEditView );
                            break;
        case KEY_PAGEDOWN:  aPaM = bCtrl ? CursorEndOfDoc() : PageDown( aPaM, pEditView );
                            break;
        case css::awt::Key::MOVE_TO_BEGIN_OF_LINE:
                            aPaM = CursorStartOfLine( aPaM );
                            fnSetStartOfLineFlag();
                            bKeyModifySelection = false;
                            break;
        case css::awt::Key::MOVE_TO_END_OF_LINE:
                            aPaM = CursorEndOfLine( aPaM );
                            fnSetEndOfLineFlag();
                            bKeyModifySelection = false;
                            break;
        case css::awt::Key::MOVE_WORD_BACKWARD:
                            aPaM = WordLeft( aPaM );
                            bKeyModifySelection = false;
                            break;
        case css::awt::Key::MOVE_WORD_FORWARD:
                            aPaM = WordRight( aPaM );
                            bKeyModifySelection = false;
                            break;
        case css::awt::Key::MOVE_TO_BEGIN_OF_PARAGRAPH:
                            aPaM = CursorStartOfParagraph( aPaM );
                            if( aPaM == aOldPaM )
                            {
                                aPaM = CursorLeft( aPaM );
                                aPaM = CursorStartOfParagraph( aPaM );
                            }
                            bKeyModifySelection = false;
                            break;
        case css::awt::Key::MOVE_TO_END_OF_PARAGRAPH:
                            aPaM = CursorEndOfParagraph( aPaM );
                            if( aPaM == aOldPaM )
                            {
                                aPaM = CursorRight( aPaM );
                                aPaM = CursorEndOfParagraph( aPaM );
                            }
                            bKeyModifySelection = false;
                            break;
        case css::awt::Key::MOVE_TO_BEGIN_OF_DOCUMENT:
                            aPaM = CursorStartOfDoc();
                            bKeyModifySelection = false;
                            break;
        case css::awt::Key::MOVE_TO_END_OF_DOCUMENT:
                            aPaM = CursorEndOfDoc();
                            bKeyModifySelection = false;
                            break;
        case css::awt::Key::SELECT_TO_BEGIN_OF_LINE:
                            aPaM = CursorStartOfLine( aPaM );
                            bKeyModifySelection = true;
                            break;
        case css::awt::Key::SELECT_TO_END_OF_LINE:
                            aPaM = CursorEndOfLine( aPaM );
                            bKeyModifySelection = true;
                            break;
        case css::awt::Key::SELECT_BACKWARD:
                            aPaM = CursorLeft( aPaM );
                            bKeyModifySelection = true;
                            break;
        case css::awt::Key::SELECT_FORWARD:
                            aPaM = CursorRight( aPaM );
                            bKeyModifySelection = true;
                            break;
        case css::awt::Key::SELECT_WORD_BACKWARD:
                            aPaM = WordLeft( aPaM );
                            bKeyModifySelection = true;
                            break;
        case css::awt::Key::SELECT_WORD_FORWARD:
                            aPaM = WordRight( aPaM );
                            bKeyModifySelection = true;
                            break;
        case css::awt::Key::SELECT_TO_BEGIN_OF_PARAGRAPH:
                            aPaM = CursorStartOfParagraph( aPaM );
                            if( aPaM == aOldPaM )
                            {
                                aPaM = CursorLeft( aPaM );
                                aPaM = CursorStartOfParagraph( aPaM );
                            }
                            bKeyModifySelection = true;
                            break;
        case css::awt::Key::SELECT_TO_END_OF_PARAGRAPH:
                            aPaM = CursorEndOfParagraph( aPaM );
                            if( aPaM == aOldPaM )
                            {
                                aPaM = CursorRight( aPaM );
                                aPaM = CursorEndOfParagraph( aPaM );
                            }
                            bKeyModifySelection = true;
                            break;
        case css::awt::Key::SELECT_TO_BEGIN_OF_DOCUMENT:
                            aPaM = CursorStartOfDoc();
                            bKeyModifySelection = true;
                            break;
        case css::awt::Key::SELECT_TO_END_OF_DOCUMENT:
                            aPaM = CursorEndOfDoc();
                            bKeyModifySelection = true;
                            break;
    }

    if ( aOldPaM != aPaM && nullptr != aOldPaM.GetNode() )
    {
        aOldPaM.GetNode()->checkAndDeleteEmptyAttribs();
    }

    // May cause, a CreateAnchor or deselection all
    maSelEngine.SetCurView(pEditView);
    maSelEngine.CursorPosChanging( bKeyModifySelection, aTranslatedKeyEvent.GetKeyCode().IsMod1() );
    EditPaM aOldEnd(pEditView->getImpl().GetEditSelection().Max());

    {
        EditSelection aNewSelection(pEditView->getImpl().GetEditSelection());
        aNewSelection.Max() = aPaM;
        pEditView->getImpl().SetEditSelection(aNewSelection);
        // const_cast<EditPaM&>(pEditView->getImpl().GetEditSelection().Max()) = aPaM;
    }

    if ( bKeyModifySelection )
    {
        // Then the selection is expanded ... or the whole selection is painted in case of tiled rendering.
        EditSelection aTmpNewSel( comphelper::LibreOfficeKit::isActive() ? pEditView->getImpl().GetEditSelection().Min() : aOldEnd, aPaM );
        pEditView->getImpl().DrawSelectionXOR( aTmpNewSel );
    }
    else
    {
        EditSelection aNewSelection(pEditView->getImpl().GetEditSelection());
        aNewSelection.Min() = aPaM;
        pEditView->getImpl().SetEditSelection(aNewSelection);
        // const_cast<EditPaM&>(pEditView->getImpl().GetEditSelection().Min()) = aPaM;
    }

    return pEditView->getImpl().GetEditSelection();
}

EditPaM ImpEditEngine::CursorVisualStartEnd( EditView const * mpEditView, const EditPaM& rPaM, bool bStart )
{
    EditPaM aPaM( rPaM );

    sal_Int32 nPara = GetEditDoc().GetPos( aPaM.GetNode() );
    ParaPortion* pParaPortion = GetParaPortions().SafeGetObject( nPara );
    if (!pParaPortion)
        return aPaM;

    sal_Int32 nLine = pParaPortion->GetLines().FindLine( aPaM.GetIndex(), false );
    const EditLine& rLine = pParaPortion->GetLines()[nLine];
    bool bEmptyLine = rLine.GetStart() == rLine.GetEnd();

    mpEditView->getImpl().maExtraCursorFlags = CursorFlags();

    if ( !bEmptyLine )
    {
        OUString aLine = aPaM.GetNode()->GetString().copy(rLine.GetStart(), rLine.GetEnd() - rLine.GetStart());

        UErrorCode nError = U_ZERO_ERROR;
        UBiDi* pBidi = ubidi_openSized( aLine.getLength(), 0, &nError );

        const UBiDiLevel  nBidiLevel = IsRightToLeft( nPara ) ? 1 /*RTL*/ : 0 /*LTR*/;
        ubidi_setPara( pBidi, reinterpret_cast<const UChar *>(aLine.getStr()), aLine.getLength(), nBidiLevel, nullptr, &nError );

        sal_Int32 nVisPos = bStart ? 0 : aLine.getLength()-1;
        const sal_Int32 nLogPos = ubidi_getLogicalIndex( pBidi, nVisPos, &nError );

        ubidi_close( pBidi );

        aPaM.SetIndex( nLogPos + rLine.GetStart() );

        sal_Int32 nTmp;
        sal_Int32 nTextPortion = pParaPortion->GetTextPortions().FindPortion( aPaM.GetIndex(), nTmp, true );
        const TextPortion& rTextPortion = pParaPortion->GetTextPortions()[nTextPortion];
        bool bPortionRTL = rTextPortion.IsRightToLeft();

        if ( bStart )
        {
            mpEditView->getImpl().SetCursorBidiLevel( bPortionRTL ? 0 : 1 );
            // Maybe we must be *behind* the character
            if (bPortionRTL && mpEditView->IsInsertMode())
                aPaM.SetIndex( aPaM.GetIndex()+1 );
        }
        else
        {
            mpEditView->getImpl().SetCursorBidiLevel( bPortionRTL ? 1 : 0 );
            if ( !bPortionRTL && mpEditView->IsInsertMode() )
                aPaM.SetIndex( aPaM.GetIndex()+1 );
        }
    }

    return aPaM;
}

EditPaM ImpEditEngine::CursorVisualLeftRight( EditView const * pEditView, const EditPaM& rPaM, sal_uInt16 nCharacterIteratorMode, bool bVisualToLeft )
{
    EditPaM aPaM( rPaM );

    sal_Int32 nPara = GetEditDoc().GetPos( aPaM.GetNode() );
    ParaPortion* pParaPortion = GetParaPortions().SafeGetObject( nPara );
    if (!pParaPortion)
        return aPaM;

    sal_Int32 nLine = pParaPortion->GetLines().FindLine( aPaM.GetIndex(), false );
    const EditLine& rLine = pParaPortion->GetLines()[nLine];
    bool bEmptyLine = rLine.GetStart() == rLine.GetEnd();

    pEditView->getImpl().maExtraCursorFlags = CursorFlags();

    bool bParaRTL = IsRightToLeft( nPara );

    bool bDone = false;

    if ( bEmptyLine )
    {
        if ( bVisualToLeft )
        {
            aPaM = CursorUp( aPaM, pEditView );
            if ( aPaM != rPaM )
                aPaM = CursorVisualStartEnd( pEditView, aPaM, false );
        }
        else
        {
            aPaM = CursorDown( aPaM, pEditView );
            if ( aPaM != rPaM )
                aPaM = CursorVisualStartEnd( pEditView, aPaM, true );
        }

        bDone = true;
    }

    bool bLogicalBackward = bParaRTL ? !bVisualToLeft : bVisualToLeft;

    if ( !bDone && pEditView->IsInsertMode() )
    {
        // Check if we are within a portion and don't have overwrite mode, then it's easy...
        sal_Int32 nPortionStart;
        sal_Int32 nTextPortion = pParaPortion->GetTextPortions().FindPortion( aPaM.GetIndex(), nPortionStart );
        const TextPortion& rTextPortion = pParaPortion->GetTextPortions()[nTextPortion];

        bool bPortionBoundary = ( aPaM.GetIndex() == nPortionStart ) || ( aPaM.GetIndex() == (nPortionStart+rTextPortion.GetLen()) );
        sal_uInt16 nRTLLevel = rTextPortion.GetRightToLeftLevel();

        // Portion boundary doesn't matter if both have same RTL level
        sal_Int32 nRTLLevelNextPortion = -1;
        if ( bPortionBoundary && aPaM.GetIndex() && ( aPaM.GetIndex() < aPaM.GetNode()->Len() ) )
        {
            sal_Int32 nTmp;
            sal_Int32 nNextTextPortion = pParaPortion->GetTextPortions().FindPortion( aPaM.GetIndex()+1, nTmp, !bLogicalBackward );
            const TextPortion& rNextTextPortion = pParaPortion->GetTextPortions()[nNextTextPortion];
            nRTLLevelNextPortion = rNextTextPortion.GetRightToLeftLevel();
        }

        if ( !bPortionBoundary || ( nRTLLevel == nRTLLevelNextPortion ) )
        {
            if (bVisualToLeft != bool(nRTLLevel % 2))
            {
                aPaM = CursorLeft( aPaM, nCharacterIteratorMode );
                pEditView->getImpl().SetCursorBidiLevel( 1 );
            }
            else
            {
                aPaM = CursorRight( aPaM, nCharacterIteratorMode );
                pEditView->getImpl().SetCursorBidiLevel( 0 );
            }
            bDone = true;
        }
    }

    if ( !bDone )
    {
        bool bGotoStartOfNextLine = false;
        bool bGotoEndOfPrevLine = false;

        OUString aLine = aPaM.GetNode()->GetString().copy(rLine.GetStart(), rLine.GetEnd() - rLine.GetStart());
        const sal_Int32 nPosInLine = aPaM.GetIndex() - rLine.GetStart();

        UErrorCode nError = U_ZERO_ERROR;
        UBiDi* pBidi = ubidi_openSized( aLine.getLength(), 0, &nError );

        const UBiDiLevel  nBidiLevel = IsRightToLeft( nPara ) ? 1 /*RTL*/ : 0 /*LTR*/;
        ubidi_setPara( pBidi, reinterpret_cast<const UChar *>(aLine.getStr()), aLine.getLength(), nBidiLevel, nullptr, &nError );

        if ( !pEditView->IsInsertMode() )
        {
            bool bEndOfLine = nPosInLine == aLine.getLength();
            sal_Int32 nVisPos = ubidi_getVisualIndex( pBidi, !bEndOfLine ? nPosInLine : nPosInLine-1, &nError );
            if ( bVisualToLeft )
            {
                bGotoEndOfPrevLine = nVisPos == 0;
                if ( !bEndOfLine )
                    nVisPos--;
            }
            else
            {
                bGotoStartOfNextLine = nVisPos == (aLine.getLength() - 1);
                if ( !bEndOfLine )
                    nVisPos++;
            }

            if ( !bGotoEndOfPrevLine && !bGotoStartOfNextLine )
            {
                aPaM.SetIndex( rLine.GetStart() + ubidi_getLogicalIndex( pBidi, nVisPos, &nError ) );
                pEditView->getImpl().SetCursorBidiLevel( 0 );
            }
        }
        else
        {
            bool bWasBehind = false;
            bool bBeforePortion = !nPosInLine || pEditView->getImpl().GetCursorBidiLevel() == 1;
            if ( nPosInLine && ( !bBeforePortion ) ) // before the next portion
                bWasBehind = true;  // step one back, otherwise visual will be unusable when rtl portion follows.

            sal_Int32 nPortionStart;
            sal_Int32 nTextPortion = pParaPortion->GetTextPortions().FindPortion( aPaM.GetIndex(), nPortionStart, bBeforePortion );
            const TextPortion& rTextPortion = pParaPortion->GetTextPortions()[nTextPortion];
            bool bRTLPortion = rTextPortion.IsRightToLeft();

            // -1: We are 'behind' the character
            tools::Long nVisPos = static_cast<tools::Long>(ubidi_getVisualIndex( pBidi, bWasBehind ? nPosInLine-1 : nPosInLine, &nError ));
            if ( bVisualToLeft )
            {
                if ( !bWasBehind || bRTLPortion )
                    nVisPos--;
            }
            else
            {
                if ( bWasBehind || bRTLPortion || bBeforePortion )
                    nVisPos++;
            }

            bGotoEndOfPrevLine = nVisPos < 0;
            bGotoStartOfNextLine = nVisPos >= aLine.getLength();

            if ( !bGotoEndOfPrevLine && !bGotoStartOfNextLine )
            {
                aPaM.SetIndex( rLine.GetStart() + ubidi_getLogicalIndex( pBidi, nVisPos, &nError ) );

                // RTL portion, stay visually on the left side.
                sal_Int32 _nPortionStart;
                // sal_uInt16 nTextPortion = pParaPortion->GetTextPortions().FindPortion( aPaM.GetIndex(), nPortionStart, !bRTLPortion );
                sal_Int32 _nTextPortion = pParaPortion->GetTextPortions().FindPortion( aPaM.GetIndex(), _nPortionStart, true );
                const TextPortion& _rTextPortion = pParaPortion->GetTextPortions()[_nTextPortion];
                if ( bVisualToLeft && !bRTLPortion && _rTextPortion.IsRightToLeft() )
                    aPaM.SetIndex( aPaM.GetIndex()+1 );
                else if ( !bVisualToLeft && bRTLPortion && ( bWasBehind || !_rTextPortion.IsRightToLeft() ) )
                    aPaM.SetIndex( aPaM.GetIndex()+1 );

                pEditView->getImpl().SetCursorBidiLevel( _nPortionStart );
            }
        }

        ubidi_close( pBidi );

        if ( bGotoEndOfPrevLine )
        {
            aPaM = CursorUp( aPaM, pEditView );
            if ( aPaM != rPaM )
                aPaM = CursorVisualStartEnd( pEditView, aPaM, false );
        }
        else if ( bGotoStartOfNextLine )
        {
            aPaM = CursorDown( aPaM, pEditView );
            if ( aPaM != rPaM )
                aPaM = CursorVisualStartEnd( pEditView, aPaM, true );
        }
    }
    return aPaM;
}


EditPaM ImpEditEngine::CursorLeft( const EditPaM& rPaM, sal_uInt16 nCharacterIteratorMode )
{
    EditPaM aCurPaM( rPaM );
    EditPaM aNewPaM( aCurPaM );

    if ( aCurPaM.GetIndex() )
    {
        sal_Int32 nCount = 1;
        uno::Reference < i18n::XBreakIterator > _xBI( ImplGetBreakIterator() );
        aNewPaM.SetIndex(
             _xBI->previousCharacters(
                 aNewPaM.GetNode()->GetString(), aNewPaM.GetIndex(), GetLocale( aNewPaM ), nCharacterIteratorMode, nCount, nCount));
    }
    else
    {
        ContentNode* pNode = aCurPaM.GetNode();
        pNode = GetPrevVisNode( pNode );
        if ( pNode )
        {
            aNewPaM.SetNode( pNode );
            aNewPaM.SetIndex( pNode->Len() );
        }
    }

    return aNewPaM;
}

EditPaM ImpEditEngine::CursorRight( const EditPaM& rPaM, sal_uInt16 nCharacterIteratorMode )
{
    EditPaM aCurPaM( rPaM );
    EditPaM aNewPaM( aCurPaM );

    if ( aCurPaM.GetIndex() < aCurPaM.GetNode()->Len() )
    {
        uno::Reference < i18n::XBreakIterator > _xBI( ImplGetBreakIterator() );
        sal_Int32 nCount = 1;
        aNewPaM.SetIndex(
            _xBI->nextCharacters(
                aNewPaM.GetNode()->GetString(), aNewPaM.GetIndex(), GetLocale( aNewPaM ), nCharacterIteratorMode, nCount, nCount));
    }
    else
    {
        ContentNode* pNode = aCurPaM.GetNode();
        pNode = GetNextVisNode( pNode );
        if ( pNode )
        {
            aNewPaM.SetNode( pNode );
            aNewPaM.SetIndex( 0 );
        }
    }

    return aNewPaM;
}

EditPaM ImpEditEngine::CursorUp( const EditPaM& rPaM, EditView const * pView )
{
    assert(pView && "No View - No Cursor Movement!");

    const ParaPortion* pPPortion = FindParaPortion( rPaM.GetNode() );
    assert(pPPortion);
    sal_Int32 nLine = pPPortion->GetLineNumber( rPaM.GetIndex() );
    assert(nLine >= 0);
    const EditLine& rLine = pPPortion->GetLines()[nLine];

    tools::Long nX;
    if ( pView->getImpl().mnTravelXPos == TRAVEL_X_DONTKNOW )
    {
        nX = GetXPos(*pPPortion, rLine, rPaM.GetIndex());
        pView->getImpl().mnTravelXPos = nX + mnOnePixelInRef;
    }
    else
        nX = pView->getImpl().mnTravelXPos;

    EditPaM aNewPaM( rPaM );
    if ( nLine )    // same paragraph
    {
        assert(nLine >= 1);
        const EditLine& rPrevLine = pPPortion->GetLines()[nLine-1];
        aNewPaM.SetIndex(GetChar(*pPPortion, rPrevLine, nX));
        // If a previous automatically wrapped line, and one has to be exactly
        // at the end of this line, the cursor lands on the current line at the
        // beginning. See Problem: Last character of an automatically wrapped
        // Row = cursor
        if ( aNewPaM.GetIndex() && ( aNewPaM.GetIndex() == rLine.GetStart() ) )
            aNewPaM = CursorLeft( aNewPaM );
    }
    else    // previous paragraph
    {
        const ParaPortion* pPrevPortion = GetPrevVisPortion( pPPortion );
        if ( pPrevPortion )
        {
            const EditLine& rLine2 = pPrevPortion->GetLines()[pPrevPortion->GetLines().Count()-1];
            aNewPaM.SetNode( pPrevPortion->GetNode() );
            aNewPaM.SetIndex(GetChar(*pPrevPortion, rLine2, nX + mnOnePixelInRef));
        }
    }

    return aNewPaM;
}

EditPaM ImpEditEngine::CursorDown( const EditPaM& rPaM, EditView const * pView )
{
    assert(pView);

    const ParaPortion* pPPortion = FindParaPortion( rPaM.GetNode() );
    assert(pPPortion);
    sal_Int32 nLine = pPPortion->GetLineNumber( rPaM.GetIndex() );

    tools::Long nX;
    if ( pView->getImpl().mnTravelXPos == TRAVEL_X_DONTKNOW )
    {
        const EditLine& rLine = pPPortion->GetLines()[nLine];
        nX = GetXPos(*pPPortion, rLine, rPaM.GetIndex());
        pView->getImpl().mnTravelXPos = nX + mnOnePixelInRef;
    }
    else
        nX = pView->getImpl().mnTravelXPos;

    EditPaM aNewPaM( rPaM );
    if ( nLine < pPPortion->GetLines().Count()-1 )
    {
        const EditLine& rNextLine = pPPortion->GetLines()[nLine+1];
        aNewPaM.SetIndex(GetChar(*pPPortion, rNextLine, nX));
        // Special treatment, see CursorUp ...
        if ( ( aNewPaM.GetIndex() == rNextLine.GetEnd() ) && ( aNewPaM.GetIndex() > rNextLine.GetStart() ) && ( aNewPaM.GetIndex() < pPPortion->GetNode()->Len() ) )
            aNewPaM = CursorLeft( aNewPaM );
    }
    else    // next paragraph
    {
        const ParaPortion* pNextPortion = GetNextVisPortion( pPPortion );
        if ( pNextPortion )
        {
            const EditLine& rLine = pNextPortion->GetLines()[0];
            aNewPaM.SetNode( pNextPortion->GetNode() );
            // Never at the very end when several lines, because then a line
            // below the cursor appears.
            aNewPaM.SetIndex(GetChar(*pNextPortion, rLine, nX + mnOnePixelInRef));
            if ( ( aNewPaM.GetIndex() == rLine.GetEnd() ) && ( aNewPaM.GetIndex() > rLine.GetStart() ) && ( pNextPortion->GetLines().Count() > 1 ) )
                aNewPaM = CursorLeft( aNewPaM );
        }
    }

    return aNewPaM;
}

EditPaM ImpEditEngine::CursorStartOfLine( const EditPaM& rPaM )
{
    const ParaPortion* pCurPortion = FindParaPortion( rPaM.GetNode() );
    assert(pCurPortion);
    sal_Int32 nLine = pCurPortion->GetLineNumber( rPaM.GetIndex() );
    const EditLine& rLine = pCurPortion->GetLines()[nLine];

    EditPaM aNewPaM( rPaM );
    aNewPaM.SetIndex( rLine.GetStart() );
    return aNewPaM;
}

EditPaM ImpEditEngine::CursorEndOfLine( const EditPaM& rPaM )
{
    const ParaPortion* pCurPortion = FindParaPortion( rPaM.GetNode() );
    assert(pCurPortion);
    sal_Int32 nLine = pCurPortion->GetLineNumber( rPaM.GetIndex() );
    const EditLine& rLine = pCurPortion->GetLines()[nLine];

    EditPaM aNewPaM( rPaM );
    aNewPaM.SetIndex( rLine.GetEnd() );
    if ( rLine.GetEnd() > rLine.GetStart() )
    {
        if ( aNewPaM.GetNode()->IsFeature( aNewPaM.GetIndex() - 1 ) )
        {
            // When a soft break, be in front of it!
            const EditCharAttrib* pNextFeature = aNewPaM.GetNode()->GetCharAttribs().FindFeature( aNewPaM.GetIndex()-1 );
            if ( pNextFeature && ( pNextFeature->GetItem()->Which() == EE_FEATURE_LINEBR ) )
                aNewPaM = CursorLeft( aNewPaM );
        }
        else if ( ( aNewPaM.GetNode()->GetChar( aNewPaM.GetIndex() - 1 ) == ' ' ) && ( aNewPaM.GetIndex() != aNewPaM.GetNode()->Len() ) )
        {
            // For a Blank in an auto wrapped line, it makes sense, to stand
            // in front of it, since the user wants to be after the word.
            // If this is changed, special treatment for Pos1 to End!
            aNewPaM = CursorLeft( aNewPaM );
        }
    }
    return aNewPaM;
}

EditPaM ImpEditEngine::CursorStartOfParagraph( const EditPaM& rPaM )
{
    EditPaM aPaM(rPaM);
    aPaM.SetIndex(0);
    return aPaM;
}

EditPaM ImpEditEngine::CursorEndOfParagraph( const EditPaM& rPaM )
{
    EditPaM aPaM(rPaM);
    aPaM.SetIndex(rPaM.GetNode()->Len());
    return aPaM;
}

EditPaM ImpEditEngine::CursorStartOfDoc()
{
    EditPaM aPaM( maEditDoc.GetObject( 0 ), 0 );
    return aPaM;
}

EditPaM ImpEditEngine::CursorEndOfDoc()
{
    ContentNode* pLastNode = maEditDoc.GetObject( maEditDoc.Count()-1 );
    ParaPortion* pLastPortion = GetParaPortions().SafeGetObject( maEditDoc.Count()-1 );
    OSL_ENSURE( pLastNode && pLastPortion, "CursorEndOfDoc: Node or Portion not found" );
    if (!(pLastNode && pLastPortion))
        return EditPaM();

    if ( !pLastPortion->IsVisible() )
    {
        pLastNode = GetPrevVisNode( pLastPortion->GetNode() );
        OSL_ENSURE( pLastNode, "No visible paragraph?" );
        if ( !pLastNode )
            pLastNode = maEditDoc.GetObject( maEditDoc.Count()-1 );
    }

    EditPaM aPaM( pLastNode, pLastNode->Len() );
    return aPaM;
}

EditPaM ImpEditEngine::PageUp( const EditPaM& rPaM, EditView const * pView )
{
    tools::Rectangle aRect = PaMtoEditCursor( rPaM );
    Point aTopLeft = aRect.TopLeft();
    aTopLeft.AdjustY( -(pView->GetVisArea().GetHeight() *9/10) );
    aTopLeft.AdjustX(mnOnePixelInRef);
    if ( aTopLeft.Y() < 0 )
    {
        aTopLeft.setY( 0 );
    }
    return GetPaM( aTopLeft );
}

EditPaM ImpEditEngine::PageDown( const EditPaM& rPaM, EditView const * pView )
{
    tools::Rectangle aRect = PaMtoEditCursor( rPaM );
    Point aBottomRight = aRect.BottomRight();
    aBottomRight.AdjustY(pView->GetVisArea().GetHeight() *9/10 );
    aBottomRight.AdjustX(mnOnePixelInRef);
    tools::Long nHeight = GetTextHeight();
    if ( aBottomRight.Y() > nHeight )
    {
        aBottomRight.setY( nHeight-2 );
    }
    return GetPaM( aBottomRight );
}

EditPaM ImpEditEngine::WordLeft( const EditPaM& rPaM )
{
    const sal_Int32 nCurrentPos = rPaM.GetIndex();
    EditPaM aNewPaM( rPaM );
    if ( nCurrentPos == 0 )
    {
        // Previous paragraph...
        sal_Int32 nCurPara = maEditDoc.GetPos( aNewPaM.GetNode() );
        ContentNode* pPrevNode = maEditDoc.GetObject( --nCurPara );
        if ( pPrevNode )
        {
            aNewPaM.SetNode( pPrevNode );
            aNewPaM.SetIndex( pPrevNode->Len() );
        }
    }
    else
    {
        // we need to increase the position by 1 when retrieving the locale
        // since the attribute for the char left to the cursor position is returned
        EditPaM aTmpPaM( aNewPaM );
        if ( aTmpPaM.GetIndex() < rPaM.GetNode()->Len() )
            aTmpPaM.SetIndex( aTmpPaM.GetIndex() + 1 );
        lang::Locale aLocale( GetLocale( aTmpPaM ) );

        uno::Reference < i18n::XBreakIterator > _xBI( ImplGetBreakIterator() );
        i18n::Boundary aBoundary =
            _xBI->getWordBoundary(aNewPaM.GetNode()->GetString(), nCurrentPos, aLocale, css::i18n::WordType::ANYWORD_IGNOREWHITESPACES, true);
        if ( aBoundary.startPos >= nCurrentPos )
            aBoundary = _xBI->previousWord(
                aNewPaM.GetNode()->GetString(), nCurrentPos, aLocale, css::i18n::WordType::ANYWORD_IGNOREWHITESPACES);
        aNewPaM.SetIndex( ( aBoundary.startPos != -1 ) ? aBoundary.startPos : 0 );
    }

    return aNewPaM;
}

EditPaM ImpEditEngine::WordRight( const EditPaM& rPaM, sal_Int16 nWordType )
{
    const sal_Int32 nMax = rPaM.GetNode()->Len();
    EditPaM aNewPaM( rPaM );
    if ( aNewPaM.GetIndex() < nMax )
    {
        // we need to increase the position by 1 when retrieving the locale
        // since the attribute for the char left to the cursor position is returned
        EditPaM aTmpPaM( aNewPaM );
        aTmpPaM.SetIndex( aTmpPaM.GetIndex() + 1 );
        lang::Locale aLocale( GetLocale( aTmpPaM ) );

        uno::Reference < i18n::XBreakIterator > _xBI( ImplGetBreakIterator() );
        i18n::Boundary aBoundary = _xBI->nextWord(
            aNewPaM.GetNode()->GetString(), aNewPaM.GetIndex(), aLocale, nWordType);
        aNewPaM.SetIndex( aBoundary.startPos );
    }
    // not 'else', maybe the index reached nMax now...
    if ( aNewPaM.GetIndex() >= nMax )
    {
        // Next paragraph ...
        sal_Int32 nCurPara = maEditDoc.GetPos( aNewPaM.GetNode() );
        ContentNode* pNextNode = maEditDoc.GetObject( ++nCurPara );
        if ( pNextNode )
        {
            aNewPaM.SetNode( pNextNode );
            aNewPaM.SetIndex( 0 );
        }
    }
    return aNewPaM;
}

EditPaM ImpEditEngine::StartOfWord( const EditPaM& rPaM )
{
    EditPaM aNewPaM( rPaM );

    // we need to increase the position by 1 when retrieving the locale
    // since the attribute for the char left to the cursor position is returned
    EditPaM aTmpPaM( aNewPaM );
    if ( aTmpPaM.GetIndex() < rPaM.GetNode()->Len() )
        aTmpPaM.SetIndex( aTmpPaM.GetIndex() + 1 );
    lang::Locale aLocale( GetLocale( aTmpPaM ) );

    uno::Reference < i18n::XBreakIterator > _xBI( ImplGetBreakIterator() );
    // tdf#135761 - since this function is only used when a selection is deleted at the left,
    // change the search preference of the word boundary from forward to backward.
    // For further details of a deletion of a selection check ImpEditEngine::DeleteLeftOrRight.
    i18n::Boundary aBoundary = _xBI->getWordBoundary(
        rPaM.GetNode()->GetString(), rPaM.GetIndex(), aLocale, css::i18n::WordType::ANYWORD_IGNOREWHITESPACES, false);

    aNewPaM.SetIndex( aBoundary.startPos );
    return aNewPaM;
}

EditPaM ImpEditEngine::EndOfWord( const EditPaM& rPaM )
{
    EditPaM aNewPaM( rPaM );

    // we need to increase the position by 1 when retrieving the locale
    // since the attribute for the char left to the cursor position is returned
    EditPaM aTmpPaM( aNewPaM );
    if ( aTmpPaM.GetIndex() < rPaM.GetNode()->Len() )
        aTmpPaM.SetIndex( aTmpPaM.GetIndex() + 1 );
    lang::Locale aLocale( GetLocale( aTmpPaM ) );

    uno::Reference < i18n::XBreakIterator > _xBI( ImplGetBreakIterator() );
    i18n::Boundary aBoundary = _xBI->getWordBoundary(
        rPaM.GetNode()->GetString(), rPaM.GetIndex(), aLocale, css::i18n::WordType::ANYWORD_IGNOREWHITESPACES, true);

    aNewPaM.SetIndex( aBoundary.endPos );
    return aNewPaM;
}

EditSelection ImpEditEngine::SelectWord( const EditSelection& rCurSel, sal_Int16 nWordType, bool bAcceptStartOfWord, bool bAcceptEndOfWord )
{
    EditSelection aNewSel( rCurSel );
    EditPaM aPaM( rCurSel.Max() );

    // we need to increase the position by 1 when retrieving the locale
    // since the attribute for the char left to the cursor position is returned
    EditPaM aTmpPaM( aPaM );
    if ( aTmpPaM.GetIndex() < aPaM.GetNode()->Len() )
        aTmpPaM.SetIndex( aTmpPaM.GetIndex() + 1 );
    lang::Locale aLocale( GetLocale( aTmpPaM ) );

    uno::Reference < i18n::XBreakIterator > _xBI( ImplGetBreakIterator() );
    sal_Int16 nType = _xBI->getWordType(
        aPaM.GetNode()->GetString(), aPaM.GetIndex(), aLocale);

    if ( nType == i18n::WordType::ANY_WORD )
    {
        i18n::Boundary aBoundary = _xBI->getWordBoundary(
            aPaM.GetNode()->GetString(), aPaM.GetIndex(), aLocale, nWordType,
            aPaM.GetNode()->GetString().getLength() == aPaM.GetIndex() ? false : true);

        // don't select when cursor at end of word
        if ( ( aBoundary.endPos > aPaM.GetIndex() || ( bAcceptEndOfWord && aBoundary.endPos == aPaM.GetIndex() ) ) &&
             ( ( aBoundary.startPos < aPaM.GetIndex() ) || ( bAcceptStartOfWord && ( aBoundary.startPos == aPaM.GetIndex() ) ) ) )
        {
            aNewSel.Min().SetIndex( aBoundary.startPos );
            aNewSel.Max().SetIndex( aBoundary.endPos );
        }
    }

    return aNewSel;
}

EditSelection ImpEditEngine::SelectSentence( const EditSelection& rCurSel )
    const
{
    uno::Reference < i18n::XBreakIterator > _xBI( ImplGetBreakIterator() );
    const EditPaM& rPaM = rCurSel.Min();
    const ContentNode* pNode = rPaM.GetNode();
    // #i50710# line breaks are marked with 0x01 - the break iterator prefers 0x0a for that
    const OUString sParagraph = pNode->GetString().replaceAll("\x01", "\x0a");
    //return Null if search starts at the beginning of the string
    sal_Int32 nStart = rPaM.GetIndex() ? _xBI->beginOfSentence( sParagraph, rPaM.GetIndex(), GetLocale( rPaM ) ) : 0;

    sal_Int32 nEnd = _xBI->endOfSentence(
        pNode->GetString(), rPaM.GetIndex(), GetLocale(rPaM));

    EditSelection aNewSel( rCurSel );
    OSL_ENSURE(pNode->Len() ? (nStart < pNode->Len()) : (nStart == 0), "sentence start index out of range");
    OSL_ENSURE(nEnd <= pNode->Len(), "sentence end index out of range");
    aNewSel.Min().SetIndex( nStart );
    aNewSel.Max().SetIndex( nEnd );
    return aNewSel;
}

bool ImpEditEngine::IsInputSequenceCheckingRequired( sal_Unicode nChar, const EditSelection& rCurSel ) const
{
    uno::Reference < i18n::XBreakIterator > _xBI( ImplGetBreakIterator() );

    // get the index that really is first
    const sal_Int32 nFirstPos = std::min(rCurSel.Min().GetIndex(), rCurSel.Max().GetIndex());

    bool bIsSequenceChecking =
        SvtCTLOptions::IsCTLFontEnabled() &&
        SvtCTLOptions::IsCTLSequenceChecking() &&
        nFirstPos != 0 && /* first char needs not to be checked */
        _xBI.is() && i18n::ScriptType::COMPLEX == _xBI->getScriptType( OUString( nChar ), 0 );

    return bIsSequenceChecking;
}

void ImpEditEngine::InitScriptTypes( sal_Int32 nPara )
{
    ParaPortion* pParaPortion = GetParaPortions().SafeGetObject( nPara );
    if (!pParaPortion)
        return;

    ScriptTypePosInfos& rTypes = pParaPortion->getScriptTypePosInfos();
    rTypes.clear();

    ContentNode* pNode = pParaPortion->GetNode();
    if ( !pNode->Len() )
        return;

    uno::Reference < i18n::XBreakIterator > _xBI( ImplGetBreakIterator() );

    OUString aText = pNode->GetString();

    // To handle fields put the character from the field in the string,
    // because endOfScript( ... ) will skip the CH_FEATURE, because this is WEAK
    const EditCharAttrib* pField = pNode->GetCharAttribs().FindNextAttrib( EE_FEATURE_FIELD, 0 );
    while ( pField )
    {
        const OUString aFldText = static_cast<const EditCharAttribField*>(pField)->GetFieldValue();
        if ( !aFldText.isEmpty() )
        {
            aText = aText.replaceAt( pField->GetStart(), 1, aFldText.subView(0,1) );
            short nFldScriptType = _xBI->getScriptType( aFldText, 0 );

            for ( sal_Int32 nCharInField = 1; nCharInField < aFldText.getLength(); nCharInField++ )
            {
                short nTmpType = _xBI->getScriptType( aFldText, nCharInField );

                // First char from field wins...
                if ( nFldScriptType == i18n::ScriptType::WEAK )
                {
                    nFldScriptType = nTmpType;
                    aText = aText.replaceAt( pField->GetStart(), 1, aFldText.subView(nCharInField,1) );
                }

                // ...  but if the first one is LATIN, and there are CJK or CTL chars too,
                // we prefer that ScriptType because we need another font.
                if ( ( nTmpType == i18n::ScriptType::ASIAN ) || ( nTmpType == i18n::ScriptType::COMPLEX ) )
                {
                    aText = aText.replaceAt( pField->GetStart(), 1, aFldText.subView(nCharInField,1) );
                    break;
                }
            }
        }
        // #112831# Last Field might go from 0xffff to 0x0000
        pField = pField->GetEnd() ? pNode->GetCharAttribs().FindNextAttrib( EE_FEATURE_FIELD, pField->GetEnd() ) : nullptr;
    }

    i18nutil::ScriptHintProvider stScriptHints;
    const EditCharAttrib* pScriptHint
        = pNode->GetCharAttribs().FindNextAttrib(EE_CHAR_SCRIPT_HINT, 0);
    while (pScriptHint)
    {
        const auto* pScriptHintValue
            = static_cast<const SvxScriptHintItem*>(pScriptHint->GetItem());
        stScriptHints.AddHint(pScriptHintValue->GetValue(), pScriptHint->GetStart(),
                              pScriptHint->GetEnd());

        pScriptHint = pScriptHint->GetEnd() ? pNode->GetCharAttribs().FindAttribRightOpen(
                                                  EE_CHAR_SCRIPT_HINT, pScriptHint->GetEnd())
                                            : nullptr;
    }

    const UBiDiLevel nInitialBidiLevel = IsRightToLeft(nPara) ? 1 /*RTL*/ : 0 /*LTR*/;
    auto pDirScanner = i18nutil::MakeDirectionChangeScanner(aText, nInitialBidiLevel);
    auto pScriptScanner = i18nutil::MakeScriptChangeScanner(
        aText, SvtLanguageOptions::GetI18NScriptTypeOfLanguage(GetDefaultLanguage()), *pDirScanner,
        stScriptHints);
    while (!pScriptScanner->AtEnd() || rTypes.empty())
    {
        auto stChange = pScriptScanner->Peek();
        rTypes.emplace_back(stChange.m_nScriptType, stChange.m_nStartIndex, stChange.m_nEndIndex);

        pScriptScanner->Advance();
    }

    // create writing direction information:
    WritingDirectionInfos& rDirInfos = pParaPortion->getWritingDirectionInfos();
    if (rDirInfos.empty())
        InitWritingDirections( nPara );
}

namespace {

struct FindByPos
{
    explicit FindByPos(sal_Int32 nPos)
        : mnPos(nPos)
    {
    }

    bool operator()(const ScriptTypePosInfos::value_type& rValue)
    {
        return rValue.nStartPos <= mnPos && rValue.nEndPos >= mnPos;
    }

private:
    sal_Int32 mnPos;
};

}

sal_uInt16 ImpEditEngine::GetI18NScriptType( const EditPaM& rPaM, sal_Int32* pEndPos ) const
{
    sal_uInt16 nScriptType = 0;

    if ( pEndPos )
        *pEndPos = rPaM.GetNode()->Len();

    if ( rPaM.GetNode()->Len() )
    {
        sal_Int32 nPara = GetEditDoc().GetPos( rPaM.GetNode() );
        const ParaPortion* pParaPortion = GetParaPortions().SafeGetObject( nPara );
        if (pParaPortion)
        {
            const ScriptTypePosInfos& rTypes = pParaPortion->getScriptTypePosInfos();

            if (rTypes.empty())
                const_cast<ImpEditEngine*>(this)->InitScriptTypes( nPara );

            const sal_Int32 nPos = rPaM.GetIndex();
            ScriptTypePosInfos::const_iterator itr = std::find_if(rTypes.begin(), rTypes.end(), FindByPos(nPos));
            if(itr != rTypes.end())
            {
                nScriptType = itr->nScriptType;
                if( pEndPos )
                    *pEndPos = itr->nEndPos;
            }
        }
    }
    return nScriptType ? nScriptType : SvtLanguageOptions::GetI18NScriptTypeOfLanguage( GetDefaultLanguage() );
}

SvtScriptType ImpEditEngine::GetItemScriptType( const EditSelection& rSel ) const
{
    EditSelection aSel( rSel );
    aSel.Adjust( maEditDoc );

    SvtScriptType nScriptType = SvtScriptType::NONE;

    sal_Int32 nStartPara = GetEditDoc().GetPos( aSel.Min().GetNode() );
    sal_Int32 nEndPara = GetEditDoc().GetPos( aSel.Max().GetNode() );

    for ( sal_Int32 nPara = nStartPara; nPara <= nEndPara; nPara++ )
    {
        const ParaPortion* pParaPortion = GetParaPortions().SafeGetObject( nPara );
        if (!pParaPortion)
            continue;

        ScriptTypePosInfos const& rTypes = pParaPortion->getScriptTypePosInfos();

        if (rTypes.empty())
            const_cast<ImpEditEngine*>(this)->InitScriptTypes( nPara );

        // find all the scripts of this range
        sal_Int32 nS = ( nPara == nStartPara ) ? aSel.Min().GetIndex() : 0;
        sal_Int32 nE = ( nPara == nEndPara ) ? aSel.Max().GetIndex() : pParaPortion->GetNode()->Len();

        //no selection, just bare cursor
        if (nStartPara == nEndPara && nS == nE)
        {
            //If we are not at the start of the paragraph we want the properties of the
            //preceding character. Otherwise get the properties of the next (or what the
            //next would have if it existed)
            if (nS != 0)
                --nS;
            else
                ++nE;
        }

        for (const ScriptTypePosInfo & rType : rTypes)
        {
            bool bStartInRange = rType.nStartPos <= nS && nS < rType.nEndPos;
            bool bEndInRange = rType.nStartPos < nE && nE <= rType.nEndPos;

            if (bStartInRange || bEndInRange)
            {
                if ( rType.nScriptType != i18n::ScriptType::WEAK )
                    nScriptType |= SvtLanguageOptions::FromI18NToSvtScriptType( rType.nScriptType );
            }
        }
    }
    return bool(nScriptType) ? nScriptType : SvtLanguageOptions::GetScriptTypeOfLanguage( GetDefaultLanguage() );
}

bool ImpEditEngine::IsScriptChange( const EditPaM& rPaM ) const
{
    bool bScriptChange = false;

    if ( rPaM.GetNode()->Len() )
    {
        sal_Int32 nPara = GetEditDoc().GetPos( rPaM.GetNode() );
        const ParaPortion* pParaPortion = GetParaPortions().SafeGetObject( nPara );
        if (pParaPortion)
        {
            ScriptTypePosInfos const& rTypes = pParaPortion->getScriptTypePosInfos();

            if (rTypes.empty())
                const_cast<ImpEditEngine*>(this)->InitScriptTypes( nPara );

            const sal_Int32 nPos = rPaM.GetIndex();
            for (const ScriptTypePosInfo & rType : rTypes)
            {
                if ( rType.nStartPos == nPos )
                {
                    bScriptChange = true;
                    break;
                }
            }
        }
    }
    return bScriptChange;
}

bool ImpEditEngine::HasScriptType( sal_Int32 nPara, sal_uInt16 nType ) const
{
    bool bTypeFound = false;

    const ParaPortion* pParaPortion = GetParaPortions().SafeGetObject( nPara );
    if (pParaPortion)
    {
        const ScriptTypePosInfos& rTypes = pParaPortion->getScriptTypePosInfos();

        if (rTypes.empty())
            const_cast<ImpEditEngine*>(this)->InitScriptTypes( nPara );

        bTypeFound = std::any_of(rTypes.begin(), rTypes.end(),
            [nType](const ScriptTypePosInfo& rType){ return rType.nScriptType == nType; });
    }
    return bTypeFound;
}

void ImpEditEngine::InitWritingDirections( sal_Int32 nPara )
{
    ParaPortion* pParaPortion = GetParaPortions().SafeGetObject( nPara );
    if (!pParaPortion)
        return;

    WritingDirectionInfos& rInfos = pParaPortion->getWritingDirectionInfos();
    rInfos.clear();

    if (pParaPortion->GetNode()->Len() && !mbFuzzing)
    {
        const OUString aText = pParaPortion->GetNode()->GetString();

        // Bidi functions from icu 2.0

        UErrorCode nError = U_ZERO_ERROR;
        UBiDi* pBidi = ubidi_openSized( aText.getLength(), 0, &nError );
        nError = U_ZERO_ERROR;

        const UBiDiLevel nBidiLevel = IsRightToLeft(nPara) ? 1 /*RTL*/ : 0 /*LTR*/;
        ubidi_setPara( pBidi, reinterpret_cast<const UChar *>(aText.getStr()), aText.getLength(), nBidiLevel, nullptr, &nError );
        nError = U_ZERO_ERROR;

        int32_t nCount = ubidi_countRuns( pBidi, &nError );

        /* ubidi_countRuns can return -1 in case of error */
        if (nCount > 0)
        {
            int32_t nStart = 0;
            int32_t nEnd;
            UBiDiLevel nCurrDir;

            for (int32_t nIdx = 0; nIdx < nCount; ++nIdx)
            {
                ubidi_getLogicalRun( pBidi, nStart, &nEnd, &nCurrDir );
                rInfos.emplace_back( nCurrDir, nStart, nEnd );
                nStart = nEnd;
            }
        }

        ubidi_close( pBidi );
    }

    // No infos mean ubidi error, default to LTR
    if ( rInfos.empty() )
        rInfos.emplace_back( 0, 0, pParaPortion->GetNode()->Len() );

}

bool ImpEditEngine::IsRightToLeft( sal_Int32 nPara ) const
{
    bool bR2L = false;
    const SvxFrameDirectionItem* pFrameDirItem = nullptr;

    if ( !IsEffectivelyVertical() )
    {
        bR2L = GetDefaultHorizontalTextDirection() == EEHorizontalTextDirection::R2L;
        pFrameDirItem = &GetParaAttrib( nPara, EE_PARA_WRITINGDIR );
        if ( pFrameDirItem->GetValue() == SvxFrameDirection::Environment )
        {
            // #103045# if DefaultHorizontalTextDirection is set, use that value, otherwise pool default.
            if ( GetDefaultHorizontalTextDirection() != EEHorizontalTextDirection::Default )
            {
                pFrameDirItem = nullptr; // bR2L already set to default horizontal text direction
            }
            else
            {
                // Use pool default
                pFrameDirItem = &GetEmptyItemSet().Get(EE_PARA_WRITINGDIR);
            }
        }
    }

    if ( pFrameDirItem )
        bR2L = pFrameDirItem->GetValue() == SvxFrameDirection::Horizontal_RL_TB;

    return bR2L;
}

bool ImpEditEngine::HasDifferentRTLLevels( const ContentNode* pNode )
{
    bool bHasDifferentRTLLevels = false;

    sal_Int32 nPara = GetEditDoc().GetPos( pNode );
    ParaPortion* pParaPortion = GetParaPortions().SafeGetObject( nPara );
    if (pParaPortion)
    {
        sal_uInt16 nRTLLevel = IsRightToLeft( nPara ) ? 1 : 0;
        for ( sal_Int32 n = 0; n < pParaPortion->GetTextPortions().Count(); n++ )
        {
            const TextPortion& rTextPortion = pParaPortion->GetTextPortions()[n];
            if ( rTextPortion.GetRightToLeftLevel() != nRTLLevel )
            {
                bHasDifferentRTLLevels = true;
                break;
            }
        }
    }
    return bHasDifferentRTLLevels;
}


sal_uInt8 ImpEditEngine::GetRightToLeft( sal_Int32 nPara, sal_Int32 nPos, sal_Int32* pStart, sal_Int32* pEnd )
{
    sal_uInt8 nRightToLeft = 0;

    ContentNode* pNode = maEditDoc.GetObject( nPara );
    if ( pNode && pNode->Len() )
    {
        ParaPortion* pParaPortion = GetParaPortions().SafeGetObject( nPara );
        if (pParaPortion)
        {
            WritingDirectionInfos& rDirInfos = pParaPortion->getWritingDirectionInfos();
            if (rDirInfos.empty())
                InitWritingDirections( nPara );

            for (const WritingDirectionInfo & rDirInfo : rDirInfos)
            {
                if ( ( rDirInfo.nStartPos <= nPos ) && ( rDirInfo.nEndPos >= nPos ) )
                {
                    nRightToLeft = rDirInfo.nType;
                    if ( pStart )
                        *pStart = rDirInfo.nStartPos;
                    if ( pEnd )
                        *pEnd = rDirInfo.nEndPos;
                    break;
                }
            }
        }
    }
    return nRightToLeft;
}

SvxAdjust ImpEditEngine::GetJustification( sal_Int32 nPara ) const
{
    SvxAdjust eJustification = SvxAdjust::Left;

    if (!maStatus.IsOutliner())
    {
        eJustification = GetParaAttrib( nPara, EE_PARA_JUST ).GetAdjust();

        if ( IsRightToLeft( nPara ) )
        {
            if ( eJustification == SvxAdjust::Left )
                eJustification = SvxAdjust::Right;
            else if ( eJustification == SvxAdjust::Right )
                eJustification = SvxAdjust::Left;
        }
    }
    return eJustification;
}

SvxCellJustifyMethod ImpEditEngine::GetJustifyMethod( sal_Int32 nPara ) const
{
    const SvxJustifyMethodItem& rItem = GetParaAttrib(nPara, EE_PARA_JUST_METHOD);
    return rItem.GetValue();
}

SvxCellVerJustify ImpEditEngine::GetVerJustification( sal_Int32 nPara ) const
{
    const SvxVerJustifyItem& rItem = GetParaAttrib(nPara, EE_PARA_VER_JUST);
    return rItem.GetValue();
}

SvxFontUnitMetrics ImpEditEngine::GetFontUnitMetrics(ContentNode* pNode)
{
    SvxFont aTmpFont{ pNode->GetCharAttribs().GetDefFont() };
    SeekCursor(pNode, /*index*/ 1, aTmpFont);
    aTmpFont.SetPhysFont(*GetRefDevice());

    // tdf#36709: Metrics conversion should use em and ic values from the bound fonts.
    // Unfortunately, this currently poses a problem due to font substitution: tests
    // abort when a missing font is set on a device.
    // In the interim, use height for all metrics. This is technically not correct, but
    // should be close enough for common fonts.
    auto dTextLineHeight = static_cast<double>(aTmpFont.GetPhysTxtSize(GetRefDevice()).Height());
    return SvxFontUnitMetrics{ /*em*/ dTextLineHeight, /*ic*/ dTextLineHeight };
}

//  Text changes
void ImpEditEngine::ImpRemoveChars( const EditPaM& rPaM, sal_Int32 nChars )
{
    if ( IsUndoEnabled() && !IsInUndo() )
    {
        const OUString aStr( rPaM.GetNode()->Copy( rPaM.GetIndex(), nChars ) );

        // Check whether attributes are deleted or changed:
        const sal_Int32 nStart = rPaM.GetIndex();
        const sal_Int32 nEnd = nStart + nChars;
        const CharAttribList::AttribsType& rAttribs = rPaM.GetNode()->GetCharAttribs().GetAttribs();
        for (const auto & rAttrib : rAttribs)
        {
            const EditCharAttrib& rAttr = *rAttrib;
            if (rAttr.GetEnd() >= nStart && rAttr.GetStart() < nEnd)
            {
                EditSelection aSel( rPaM );
                aSel.Max().SetIndex( aSel.Max().GetIndex() + nChars );
                InsertUndo( CreateAttribUndo( aSel, GetEmptyItemSet() ) );
                break;  // for
            }
        }
        InsertUndo(std::make_unique<EditUndoRemoveChars>(mpEditEngine, CreateEPaM(rPaM), aStr));
    }

    maEditDoc.RemoveChars( rPaM, nChars );
}

EditSelection ImpEditEngine::ImpMoveParagraphs( Range aOldPositions, sal_Int32 nNewPos )
{
    aOldPositions.Normalize();
    bool bValidAction = ( static_cast<tools::Long>(nNewPos) < aOldPositions.Min() ) || ( static_cast<tools::Long>(nNewPos) > aOldPositions.Max() );
    OSL_ENSURE( bValidAction, "Move in itself?" );
    OSL_ENSURE( aOldPositions.Max() <= static_cast<tools::Long>(GetParaPortions().Count()), "totally over it: MoveParagraphs" );

    EditSelection aSelection;

    if ( !bValidAction )
    {
        aSelection = maEditDoc.GetStartPaM();
        return aSelection;
    }

    sal_Int32 nParaCount = GetParaPortions().Count();

    if ( nNewPos >= nParaCount )
        nNewPos = nParaCount;

    // Height may change when moving first or last Paragraph
    ParaPortion* pRecalc1 = nullptr;
    ParaPortion* pRecalc2 = nullptr;
    ParaPortion* pRecalc3 = nullptr;
    ParaPortion* pRecalc4 = nullptr;

    if (nNewPos == 0) // Move to Start
    {
        if (GetParaPortions().exists(0))
            pRecalc1 = &GetParaPortions().getRef(0);
        if (GetParaPortions().exists(aOldPositions.Min()))
            pRecalc2 = &GetParaPortions().getRef(aOldPositions.Min());

    }
    else if (nNewPos == nParaCount)
    {
        if (GetParaPortions().exists(nParaCount - 1))
            pRecalc1 = &GetParaPortions().getRef(nParaCount - 1);
        if (GetParaPortions().exists(aOldPositions.Max()))
            pRecalc2 = &GetParaPortions().getRef(aOldPositions.Max());
    }

    if (aOldPositions.Min() == 0) // Move from Start
    {
        if (GetParaPortions().exists(0))
            pRecalc3 = &GetParaPortions().getRef(0);
        if (GetParaPortions().exists(aOldPositions.Max() + 1))
            pRecalc4 = &GetParaPortions().getRef(aOldPositions.Max() + 1);
    }
    else if (aOldPositions.Max() == nParaCount - 1)
    {
        if (GetParaPortions().exists(aOldPositions.Max()))
            pRecalc3 = &GetParaPortions().getRef(aOldPositions.Max());
        if (GetParaPortions().exists(aOldPositions.Min() - 1))
            pRecalc4 = &GetParaPortions().getRef(aOldPositions.Min() - 1);
    }

    MoveParagraphsInfo aMoveParagraphsInfo( aOldPositions.Min(), aOldPositions.Max(), nNewPos );
    maBeginMovingParagraphsHdl.Call( aMoveParagraphsInfo );

    if ( IsUndoEnabled() && !IsInUndo())
        InsertUndo(std::make_unique<EditUndoMoveParagraphs>(mpEditEngine, aOldPositions, nNewPos));

    // do not lose sight of the Position !
    ParaPortion* pDestPortion = GetParaPortions().SafeGetObject( nNewPos );

    // Temporary containers used for moving the paragraph portions and content nodes to a new location
    std::vector<std::unique_ptr<ParaPortion>> aParagraphPortionVector;
    std::vector<std::unique_ptr<ContentNode>> aContentNodeVector;

    // Take the paragraph portions and content nodes out of its containers
    for (tools::Long i = aOldPositions.Min(); i <= aOldPositions.Max(); i++  )
    {
        // always aOldPositions.Min() as the index, since we remove and the elements from the containers and the
        // other elements shift to the left.
        std::unique_ptr<ParaPortion> pPortion = GetParaPortions().Release(aOldPositions.Min());
        aParagraphPortionVector.push_back(std::move(pPortion));

        std::unique_ptr<ContentNode> pContentNode = maEditDoc.Release(aOldPositions.Min());
        aContentNodeVector.push_back(std::move(pContentNode));
    }

    // Determine the new location for paragraphs
    sal_Int32 nRealNewPos = pDestPortion ? GetParaPortions().GetPos( pDestPortion ) : GetParaPortions().Count();
    assert(nRealNewPos != EE_PARA_MAX && "ImpMoveParagraphs: Invalid Position!");

    // Add the paragraph portions and content nodes to a new position
    sal_Int32 i = 0;
    for (auto& pPortion : aParagraphPortionVector)
    {
        if (i == 0)
            aSelection.Min().SetNode(pPortion->GetNode());
        aSelection.Max().SetNode(pPortion->GetNode());
        aSelection.Max().SetIndex(pPortion->GetNode()->Len());

        maEditDoc.Insert(nRealNewPos + i, std::move(aContentNodeVector[i]));
        GetParaPortions().Insert(nRealNewPos + i, std::move(pPortion));

        ++i;
    }

    // Signal end of paragraph moving
    maEndMovingParagraphsHdl.Call( aMoveParagraphsInfo );

    if ( GetNotifyHdl().IsSet() )
    {
        EENotify aNotify( EE_NOTIFY_PARAGRAPHSMOVED );
        aNotify.nParagraph = nNewPos;
        aNotify.nParam1 = aOldPositions.Min();
        aNotify.nParam2 = aOldPositions.Max();
        GetNotifyHdl().Call( aNotify );
    }

    maEditDoc.SetModified( true );

    if (pRecalc1)
        CalcHeight(*pRecalc1);
    if (pRecalc2)
        CalcHeight(*pRecalc2);
    if (pRecalc3)
        CalcHeight(*pRecalc3);
    if (pRecalc4)
        CalcHeight(*pRecalc4);

#if OSL_DEBUG_LEVEL > 0 && !defined NDEBUG
    ParaPortionList::DbgCheck(GetParaPortions(), maEditDoc);
#endif
    return aSelection;
}


EditPaM ImpEditEngine::ImpConnectParagraphs(ContentNode* pLeft, ContentNode* pRight,
        bool bBackward, bool const isUpdateCursors)
{
    OSL_ENSURE( pLeft != pRight, "Join together the same paragraph ?" );
    OSL_ENSURE(maEditDoc.GetPos(pLeft) != EE_PARA_MAX, "Inserted node not found (1)");
    OSL_ENSURE(maEditDoc.GetPos(pRight) != EE_PARA_MAX, "Inserted node not found (2)");

    // #i120020# it is possible that left and right are *not* in the desired order (left/right)
    // so correct it. This correction is needed, else an invalid SfxLinkUndoAction will be
    // created from ConnectParagraphs below. Assert this situation, it should be corrected by the
    // caller.
    if (maEditDoc.GetPos( pLeft ) > maEditDoc.GetPos( pRight ))
    {
        OSL_ENSURE(false, "ImpConnectParagraphs with wrong order of pLeft/pRight nodes (!)");
        std::swap(pLeft, pRight);
    }

    sal_Int32 nParagraphTobeDeleted = maEditDoc.GetPos( pRight );
    maDeletedNodes.push_back(std::make_unique<DeletedNodeInfo>( pRight, nParagraphTobeDeleted ));
    ESelection const deleted{nParagraphTobeDeleted - 1, pLeft->Len(), nParagraphTobeDeleted, 0};

    GetEditEnginePtr()->ParagraphConnected( maEditDoc.GetPos( pLeft ), maEditDoc.GetPos( pRight ) );

    if ( IsUndoEnabled() && !IsInUndo() )
    {
        InsertUndo( std::make_unique<EditUndoConnectParas>(mpEditEngine,
            maEditDoc.GetPos( pLeft ), pLeft->Len(),
            pLeft->GetContentAttribs().GetItems(), pRight->GetContentAttribs().GetItems(),
            pLeft->GetStyleSheet(), pRight->GetStyleSheet(), bBackward ) );
    }

    if ( bBackward )
    {
        pLeft->SetStyleSheet( pRight->GetStyleSheet() );
        // it feels wrong to set pLeft's attribs if pRight is empty, tdf#128046
        if ( pRight->Len() )
            pLeft->GetContentAttribs().GetItems().Set( pRight->GetContentAttribs().GetItems() );
        pLeft->GetCharAttribs().GetDefFont() = pRight->GetCharAttribs().GetDefFont();
    }

    ParaAttribsChanged( pLeft, true );

    // First search for Portions since pRight is gone after ConnectParagraphs.
    ParaPortion* pLeftPortion = FindParaPortion( pLeft );
    assert(pLeftPortion);

    if ( GetStatus().DoOnlineSpelling() )
    {
        sal_Int32 nEnd = pLeft->Len();
        sal_Int32 nInv = nEnd ? nEnd-1 : nEnd;
        pLeft->GetWrongList()->ClearWrongs( nInv, static_cast<size_t>(-1), pLeft );  // Possibly remove one
        pLeft->GetWrongList()->SetInvalidRange(nInv, nEnd+1);
        // Take over misspelled words
        WrongList* pRWrongs = pRight->GetWrongList();
        for (auto & elem : *pRWrongs)
        {
            if (elem.mnStart != 0)   // Not a subsequent
            {
                elem.mnStart = elem.mnStart + nEnd;
                elem.mnEnd = elem.mnEnd + nEnd;
                pLeft->GetWrongList()->push_back(elem);
            }
        }
    }

    if ( IsCallParaInsertedOrDeleted() )
        GetEditEnginePtr()->ParagraphDeleted( nParagraphTobeDeleted );

    EditPaM aPaM = maEditDoc.ConnectParagraphs( pLeft, pRight );
    GetParaPortions().Remove( nParagraphTobeDeleted );

    pLeftPortion->MarkSelectionInvalid( aPaM.GetIndex() );

    // the right node is deleted by EditDoc:ConnectParagraphs().
    if ( GetTextRanger() )
    {
        // By joining together the two, the left is although reformatted,
        // however if its height does not change then the formatting receives
        // the change of the total text height too late...
        for ( sal_Int32 n = nParagraphTobeDeleted; n < GetParaPortions().Count(); n++ )
        {
            ParaPortion& rParaPortion = GetParaPortions().getRef(n);
            rParaPortion.MarkSelectionInvalid(0);
            rParaPortion.GetLines().Reset();
        }
    }

    if (isUpdateCursors)
    {
        UpdateSelectionsDelete(deleted);
    }

    TextModified();

    return aPaM;
}

EditPaM ImpEditEngine::DeleteLeftOrRight( const EditSelection& rSel, sal_uInt8 nMode, DeleteMode nDelMode )
{
    OSL_ENSURE( !rSel.DbgIsBuggy( maEditDoc ), "Index out of range in DeleteLeftOrRight" );

    if ( rSel.HasRange() )  // only then Delete Selection
        return ImpDeleteSelection( rSel );

    EditPaM aCurPos( rSel.Max() );
    EditPaM aDelStart( aCurPos );
    EditPaM aDelEnd( aCurPos );
    if ( nMode == DEL_LEFT )
    {
        if ( nDelMode == DeleteMode::Simple )
        {
            sal_uInt16 nCharMode = i18n::CharacterIteratorMode::SKIPCHARACTER;
            // If we are deleting a variation selector, we want to delete the
            // whole sequence (cell).
            sal_Int32 nIndex = aCurPos.GetIndex();
            if (nIndex > 0)
            {
                const OUString& rString = aCurPos.GetNode()->GetString();
                sal_Int32 nCode = rString.iterateCodePoints(&nIndex, -1);
                if (unicode::isVariationSelector(nCode))
                    nCharMode = i18n::CharacterIteratorMode::SKIPCELL;
            }
            aDelStart = CursorLeft(aCurPos, nCharMode);
        }
        else if ( nDelMode == DeleteMode::RestOfWord )
        {
            aDelStart = StartOfWord( aCurPos );
            if ( aDelStart.GetIndex() == aCurPos.GetIndex() )
                aDelStart = WordLeft( aCurPos );
        }
        else    // DELMODE_RESTOFCONTENT
        {
            aDelStart.SetIndex( 0 );
            if ( aDelStart == aCurPos )
            {
                // Complete paragraph previous
                ContentNode* pPrev = GetPrevVisNode( aCurPos.GetNode() );
                if ( pPrev )
                    aDelStart = EditPaM( pPrev, 0 );
            }
        }
    }
    else
    {
        if ( nDelMode == DeleteMode::Simple )
        {
            aDelEnd = CursorRight( aCurPos );
        }
        else if ( nDelMode == DeleteMode::RestOfWord )
        {
            aDelEnd = EndOfWord( aCurPos );
            if (aDelEnd.GetIndex() == aCurPos.GetIndex())
            {
                const sal_Int32 nLen(aCurPos.GetNode()->Len());
                // end of para?
                if (aDelEnd.GetIndex() == nLen)
                {
                    ContentNode* pNext = GetNextVisNode( aCurPos.GetNode() );
                    if ( pNext )
                        aDelEnd = EditPaM( pNext, 0 );
                }
                else // there's still something to delete on the right
                {
                    aDelEnd = EndOfWord( WordRight( aCurPos ) );
                }
            }
        }
        else    // DELMODE_RESTOFCONTENT
        {
            aDelEnd.SetIndex( aCurPos.GetNode()->Len() );
            if ( aDelEnd == aCurPos )
            {
                // Complete paragraph next
                ContentNode* pNext = GetNextVisNode( aCurPos.GetNode() );
                if ( pNext )
                    aDelEnd = EditPaM( pNext, pNext->Len() );
            }
        }
    }

    // ConnectParagraphs not enough for different Nodes when
    // DeleteMode::RestOfContent.
    if ( ( nDelMode == DeleteMode::RestOfContent ) || ( aDelStart.GetNode() == aDelEnd.GetNode() ) )
        return ImpDeleteSelection( EditSelection( aDelStart, aDelEnd ) );

    return ImpConnectParagraphs(aDelStart.GetNode(), aDelEnd.GetNode());
}

EditPaM ImpEditEngine::ImpDeleteSelection(const EditSelection& rCurSel)
{
    if ( !rCurSel.HasRange() )
        return rCurSel.Min();

    ESelection const deleted{CreateESel(rCurSel)};
    EditSelection aCurSel(rCurSel);
    aCurSel.Adjust( maEditDoc );
    EditPaM aStartPaM(aCurSel.Min());
    EditPaM aEndPaM(aCurSel.Max());

    if( nullptr != aStartPaM.GetNode() )
        aStartPaM.GetNode()->checkAndDeleteEmptyAttribs(); // only so that newly set Attributes disappear...
    if( nullptr != aEndPaM.GetNode() )
        aEndPaM.GetNode()->checkAndDeleteEmptyAttribs(); // only so that newly set Attributes disappear...

    OSL_ENSURE( aStartPaM.GetIndex() <= aStartPaM.GetNode()->Len(), "Index out of range in ImpDeleteSelection" );
    OSL_ENSURE( aEndPaM.GetIndex() <= aEndPaM.GetNode()->Len(), "Index out of range in ImpDeleteSelection" );

    sal_Int32 nStartNode = maEditDoc.GetPos( aStartPaM.GetNode() );
    sal_Int32 nEndNode = maEditDoc.GetPos( aEndPaM.GetNode() );

    assert(nEndNode != EE_PARA_MAX && "Start > End ?!");
    assert( nStartNode <= nEndNode && "Start > End ?!" );

    // Remove all nodes in between...
    for ( sal_Int32 z = nStartNode+1; z < nEndNode; z++ )
    {
        // Always nStartNode+1, due to Remove()!
        ImpRemoveParagraph( nStartNode+1 );
    }

    if ( aStartPaM.GetNode() != aEndPaM.GetNode() )
    {
        // The Rest of the StartNodes...
        ImpRemoveChars( aStartPaM, aStartPaM.GetNode()->Len() - aStartPaM.GetIndex() );
        ParaPortion* pPortion = FindParaPortion( aStartPaM.GetNode() );
        assert(pPortion);
        pPortion->MarkSelectionInvalid( aStartPaM.GetIndex() );

        // The beginning of the EndNodes...
        const sal_Int32 nChars = aEndPaM.GetIndex();
        aEndPaM.SetIndex( 0 );
        ImpRemoveChars( aEndPaM, nChars );
        pPortion = FindParaPortion( aEndPaM.GetNode() );
        assert(pPortion);
        pPortion->MarkSelectionInvalid( 0 );
        // Join together...
        aStartPaM = ImpConnectParagraphs(aStartPaM.GetNode(), aEndPaM.GetNode(), false, false);
    }
    else
    {
        ImpRemoveChars( aStartPaM, aEndPaM.GetIndex() - aStartPaM.GetIndex() );
        ParaPortion* pPortion = FindParaPortion( aStartPaM.GetNode() );
        assert(pPortion);
        pPortion->MarkInvalid( aEndPaM.GetIndex(), aStartPaM.GetIndex() - aEndPaM.GetIndex() );
    }

    UpdateSelectionsDelete(deleted);
    UpdateSelections();
    TextModified();
    return aStartPaM;
}

void ImpEditEngine::RemoveParagraph( sal_Int32 nPara )
{
    DBG_ASSERT(maEditDoc.Count() > 1, "The first paragraph should not be deleted!");
    if (maEditDoc.Count() <= 1)
        return;

    ContentNode* pNode = maEditDoc.GetObject(nPara);
    const ParaPortion* pPortion = maParaPortionList.SafeGetObject(nPara);
    DBG_ASSERT( pPortion && pNode, "Paragraph not found: RemoveParagraph" );
    if ( pNode && pPortion )
    {
        // No Undo encapsulation needed.
        auto const len{pNode->Len()};
        ImpRemoveParagraph(nPara);
        InvalidateFromParagraph(nPara);
        ESelection const deleted{nPara == 0 ? nPara : nPara - 1,
            nPara == 0 ? 0 : maEditDoc.GetObject(nPara-1)->Len(),
            nPara, len};
        UpdateSelectionsDelete(deleted);
        UpdateSelections();
        if (IsUpdateLayout())
            FormatAndLayout();
    }
}

void ImpEditEngine::ImpRemoveParagraph( sal_Int32 nPara )
{
    assert(maEditDoc.GetObject(nPara));

    ContentNode* pNextNode = maEditDoc.GetObject( nPara+1 );

    std::unique_ptr<ContentNode> pNode = maEditDoc.Release(nPara);
    maDeletedNodes.push_back(std::make_unique<DeletedNodeInfo>(pNode.get(), nPara));

    // The node is managed by the undo and possibly destroyed!

    GetParaPortions().Remove( nPara );

    if ( IsCallParaInsertedOrDeleted() )
    {
        GetEditEnginePtr()->ParagraphDeleted( nPara );
    }

    // Extra-Space may be determined again in the following. For
    // ParaAttribsChanged the paragraph is unfortunately formatted again,
    // however this method should not be time critical!
    if ( pNextNode )
        ParaAttribsChanged( pNextNode );

    if (IsUndoEnabled() && !IsInUndo())
    {
        InsertUndo(std::make_unique<EditUndoDelContent>(mpEditEngine, std::move(pNode), nPara));
    }
    else
    {
        if ( pNode->GetStyleSheet() )
            EndListening(*pNode->GetStyleSheet());
        pNode.reset();
    }
}

EditPaM ImpEditEngine::AutoCorrect( const EditSelection& rCurSel, sal_Unicode c,
                                    bool bOverwrite, vcl::Window const * pFrameWin )
{
    // i.e. Calc has special needs regarding a leading single quotation mark
    // when starting cell input.
    if (c == '\'' && !IsReplaceLeadingSingleQuotationMark() &&
            rCurSel.Min() == rCurSel.Max() && rCurSel.Max().GetIndex() == 0)
    {
        return InsertTextUserInput( rCurSel, c, bOverwrite );
    }

    EditSelection aSel( rCurSel );
    SvxAutoCorrect* pAutoCorrect = SvxAutoCorrCfg::Get().GetAutoCorrect();
    if ( pAutoCorrect )
    {
        if ( aSel.HasRange() )
            aSel = ImpDeleteSelection( rCurSel );

        // #i78661 allow application to turn off capitalization of
        // start sentence explicitly.
        // (This is done by setting IsFirstWordCapitalization to sal_False.)
        bool bOldCapitalStartSentence = pAutoCorrect->IsAutoCorrFlag( ACFlags::CapitalStartSentence );
        if (!IsFirstWordCapitalization())
        {
            ESelection aESel( CreateESel(aSel) );
            EditSelection aFirstWordSel;
            EditSelection aSecondWordSel;
            if (aESel.end.nPara == 0)    // is this the first para?
            {
                // select first word...
                // start by checking if para starts with word.
                aFirstWordSel = SelectWord( CreateSel(ESelection()) );
                if (aFirstWordSel.Min().GetIndex() == 0 && aFirstWordSel.Max().GetIndex() == 0)
                {
                    // para does not start with word -> select next/first word
                    EditPaM aRightWord( WordRight( aFirstWordSel.Max() ) );
                    aFirstWordSel = SelectWord( EditSelection( aRightWord ) );
                }

                // select second word
                // (sometimes aSel might not point to the end of the first word
                // but to some following char like '.'. ':', ...
                // In those cases we need aSecondWordSel to see if aSel
                // will actually effect the first word.)
                EditPaM aRight2Word( WordRight( aFirstWordSel.Max() ) );
                aSecondWordSel = SelectWord( EditSelection( aRight2Word ) );
            }
            bool bIsFirstWordInFirstPara = aESel.end.nPara == 0 &&
                    aFirstWordSel.Max().GetIndex() <= aSel.Max().GetIndex() &&
                    aSel.Max().GetIndex() <= aSecondWordSel.Min().GetIndex();

            if (bIsFirstWordInFirstPara)
                pAutoCorrect->SetAutoCorrFlag( ACFlags::CapitalStartSentence, IsFirstWordCapitalization() );
        }

        ContentNode* pNode = aSel.Max().GetNode();
        const sal_Int32 nIndex = aSel.Max().GetIndex();
        EdtAutoCorrDoc aAuto(mpEditEngine, pNode, nIndex, c);
        // FIXME: this _must_ be called with reference to the actual node text!
        OUString const& rNodeString(pNode->GetString());
        pAutoCorrect->DoAutoCorrect(
            aAuto, rNodeString, nIndex, c, !bOverwrite, mbNbspRunNext, pFrameWin );
        aSel.Max().SetIndex( aAuto.GetCursor() );

        // #i78661 since the SvxAutoCorrect object used here is
        // shared we need to reset the value to its original state.
        pAutoCorrect->SetAutoCorrFlag( ACFlags::CapitalStartSentence, bOldCapitalStartSentence );
    }
    return aSel.Max();
}


EditPaM ImpEditEngine::InsertTextUserInput( const EditSelection& rCurSel,
        sal_Unicode c, bool bOverwrite )
{
    OSL_ENSURE( c != '\t', "Tab for InsertText ?" );
    OSL_ENSURE( c != '\n', "Word wrapping for InsertText ?");

    EditPaM aPaM( rCurSel.Min() );

    bool bDoOverwrite = bOverwrite &&
            ( aPaM.GetIndex() < aPaM.GetNode()->Len() );

    bool bUndoAction = ( rCurSel.HasRange() || bDoOverwrite );

    if ( bUndoAction )
        UndoActionStart( EDITUNDO_INSERT );

    if ( rCurSel.HasRange() )
    {
        aPaM = ImpDeleteSelection( rCurSel );
    }
    else if ( bDoOverwrite )
    {
        // If selected, then do not also overwrite a character!
        EditSelection aTmpSel( aPaM );
        aTmpSel.Max().SetIndex( aTmpSel.Max().GetIndex()+1 );
        OSL_ENSURE( !aTmpSel.DbgIsBuggy( maEditDoc ), "Overwrite: Wrong selection! ");
        ImpDeleteSelection( aTmpSel );
    }

    if ( aPaM.GetNode()->Len() < MAXCHARSINPARA )
    {
        if (IsInputSequenceCheckingRequired( c, rCurSel ))
        {
            uno::Reference < i18n::XExtendedInputSequenceChecker > _xISC( ImplGetInputSequenceChecker() );

            if (_xISC)
            {
                const sal_Int32 nTmpPos = aPaM.GetIndex();
                sal_Int16 nCheckMode = SvtCTLOptions::IsCTLSequenceCheckingRestricted() ?
                        i18n::InputSequenceCheckMode::STRICT : i18n::InputSequenceCheckMode::BASIC;

                // the text that needs to be checked is only the one
                // before the current cursor position
                const OUString aOldText( aPaM.GetNode()->Copy(0, nTmpPos) );
                OUString aNewText( aOldText );
                if (SvtCTLOptions::IsCTLSequenceCheckingTypeAndReplace())
                {
                    _xISC->correctInputSequence(aNewText, nTmpPos - 1, c, nCheckMode);

                    // find position of first character that has changed
                    sal_Int32 nOldLen = aOldText.getLength();
                    sal_Int32 nNewLen = aNewText.getLength();
                    const sal_Unicode *pOldTxt = aOldText.getStr();
                    const sal_Unicode *pNewTxt = aNewText.getStr();
                    sal_Int32 nChgPos = 0;
                    while ( nChgPos < nOldLen && nChgPos < nNewLen &&
                            pOldTxt[nChgPos] == pNewTxt[nChgPos] )
                        ++nChgPos;

                    const OUString aChgText( aNewText.copy( nChgPos ) );

                    // select text from first pos to be changed to current pos
                    EditSelection aSel( EditPaM( aPaM.GetNode(), nChgPos ), aPaM );

                    if (!aChgText.isEmpty())
                        return InsertText( aSel, aChgText ); // implicitly handles undo
                    else
                        return aPaM;
                }
                else
                {
                    // should the character be ignored (i.e. not get inserted) ?
                    if (!_xISC->checkInputSequence( aOldText, nTmpPos - 1, c, nCheckMode ))
                        return aPaM;    // nothing to be done -> no need for undo
                }
            }

            // at this point now we will insert the character 'normally' some lines below...
        }

        if ( IsUndoEnabled() && !IsInUndo() )
        {
            std::unique_ptr<EditUndoInsertChars> pNewUndo(new EditUndoInsertChars(mpEditEngine, CreateEPaM(aPaM), OUString(c)));
            bool bTryMerge = !bDoOverwrite && ( c != ' ' );
            InsertUndo( std::move(pNewUndo), bTryMerge );
        }

        maEditDoc.InsertText( aPaM, OUStringChar(c) );
        ParaPortion* pPortion = FindParaPortion( aPaM.GetNode() );
        assert(pPortion);
        pPortion->MarkInvalid( aPaM.GetIndex(), 1 );
        aPaM.SetIndex( aPaM.GetIndex()+1 );   // does not do EditDoc-Method anymore
    }

    TextModified();

    if ( bUndoAction )
        UndoActionEnd();

    return aPaM;
}

EditPaM ImpEditEngine::ImpInsertText(const EditSelection& aCurSel, const OUString& rStr)
{
    UndoActionStart( EDITUNDO_INSERT );

    EditPaM aPaM;
    if ( aCurSel.HasRange() )
        aPaM = ImpDeleteSelection( aCurSel );
    else
        aPaM = aCurSel.Max();

    EditPaM aCurPaM( aPaM );    // for the Invalidate

    // get word boundaries in order to clear possible WrongList entries
    // and invalidate all the necessary text (everything after and including the
    // start of the word)
    // #i107201# do the expensive SelectWord call only if online spelling is active
    EditSelection aCurWord;
    if ( GetStatus().DoOnlineSpelling() )
        aCurWord = SelectWord( EditSelection(aCurPaM), i18n::WordType::DICTIONARY_WORD );

    OUString aText(convertLineEnd(rStr, LINEEND_LF));
    if (mbFuzzing)    //tab expansion performance in editeng is appalling
        aText = aText.replaceAll("\t","-");
    SfxVoidItem aTabItem( EE_FEATURE_TAB );

    // Converts to linesep = \n
    // Token LINE_SEP query,
    // since the MAC-Compiler makes something else from \n !

    sal_Int32 nStart = 0;
    while ( nStart < aText.getLength() )
    {
        sal_Int32 nEnd = !maStatus.IsSingleLine() ?
            aText.indexOf( LINE_SEP, nStart ) : -1;
        if ( nEnd == -1 )
            nEnd = aText.getLength(); // not dereference!

        // Start == End => empty line
        if ( nEnd > nStart )
        {
            OUString aLine = aText.copy( nStart, nEnd-nStart );
            sal_Int32 nExistingChars = aPaM.GetNode()->Len();
            sal_Int32 nChars = nExistingChars + aLine.getLength();
            if (nChars > MAXCHARSINPARA)
            {
                sal_Int32 nMaxNewChars = std::max<sal_Int32>(0, MAXCHARSINPARA - nExistingChars);
                // Wherever we break, it may be wrong. However, try to find the
                // previous non-alnum/non-letter character. Note this is only
                // in the to be appended data, otherwise already existing
                // characters would have to be moved and PaM to be updated.
                // Restrict to 2*42, if not found by then assume other data or
                // language-script uses only letters or idiographs.
                sal_Int32 nPos = nMaxNewChars;
                while (nPos-- > 0 && (nMaxNewChars - nPos) <= 84)
                {
                    auto nNextPos = nPos;
                    const auto c = aLine.iterateCodePoints(&nNextPos);
                    switch (unicode::getUnicodeType(c))
                    {
                        case css::i18n::UnicodeType::UPPERCASE_LETTER:
                        case css::i18n::UnicodeType::LOWERCASE_LETTER:
                        case css::i18n::UnicodeType::TITLECASE_LETTER:
                        case css::i18n::UnicodeType::MODIFIER_LETTER:
                        case css::i18n::UnicodeType::OTHER_LETTER:
                        case css::i18n::UnicodeType::DECIMAL_DIGIT_NUMBER:
                        case css::i18n::UnicodeType::LETTER_NUMBER:
                        case css::i18n::UnicodeType::OTHER_NUMBER:
                        case css::i18n::UnicodeType::CURRENCY_SYMBOL:
                        break;
                        default:
                            {
                                // Ignore NO-BREAK spaces, NBSP, NNBSP, ZWNBSP.
                                if (c == 0x00A0 || c == 0x202F || c == 0xFEFF)
                                    break;
                                const auto n = aLine.iterateCodePoints(&nNextPos, 0);
                                if (c == '-' && nNextPos < nMaxNewChars)
                                {
                                    // Keep HYPHEN-MINUS with a number to the right.
                                    const sal_Int16 t = unicode::getUnicodeType(n);
                                    if (    t == css::i18n::UnicodeType::DECIMAL_DIGIT_NUMBER ||
                                            t == css::i18n::UnicodeType::LETTER_NUMBER ||
                                            t == css::i18n::UnicodeType::OTHER_NUMBER)
                                        nMaxNewChars = nPos;        // line break before
                                    else
                                        nMaxNewChars = nNextPos;    // line break after
                                }
                                else
                                {
                                    nMaxNewChars = nNextPos;        // line break after
                                }
                                nPos = 0;   // will break loop
                            }
                    }
                }
                // Remaining characters end up in the next paragraph. Note that
                // new nStart will be nEnd+1 below so decrement by one more.
                nEnd -= (aLine.getLength() - nMaxNewChars + 1);
                aLine = aLine.copy( 0, nMaxNewChars );  // Delete the Rest...
            }
            if ( IsUndoEnabled() && !IsInUndo() )
                InsertUndo(std::make_unique<EditUndoInsertChars>(mpEditEngine, CreateEPaM(aPaM), aLine));
            // Tabs ?
            if ( aLine.indexOf( '\t' ) == -1 )
                aPaM = maEditDoc.InsertText( aPaM, aLine );
            else
            {
                sal_Int32 nStart2 = 0;
                while ( nStart2 < aLine.getLength() )
                {
                    sal_Int32 nEnd2 = aLine.indexOf( "\t", nStart2 );
                    if ( nEnd2 == -1 )
                        nEnd2 = aLine.getLength();    // not dereference!

                    if ( nEnd2 > nStart2 )
                        aPaM = maEditDoc.InsertText( aPaM, aLine.copy( nStart2, nEnd2-nStart2 ) );
                    if ( nEnd2 < aLine.getLength() )
                    {
                        aPaM = maEditDoc.InsertFeature( aPaM, aTabItem );
                    }
                    nStart2 = nEnd2+1;
                }
            }
            ParaPortion* pPortion = FindParaPortion( aPaM.GetNode() );
            assert(pPortion);

            if ( GetStatus().DoOnlineSpelling() )
            {
                // now remove the Wrongs (red spell check marks) from both words...
                WrongList *pWrongs = aCurPaM.GetNode()->GetWrongList();
                if (pWrongs && !pWrongs->empty())
                    pWrongs->ClearWrongs( aCurWord.Min().GetIndex(), aPaM.GetIndex(), aPaM.GetNode() );
                // ... and mark both words as 'to be checked again'
                pPortion->MarkInvalid( aCurWord.Min().GetIndex(), aLine.getLength() );
            }
            else
                pPortion->MarkInvalid( aCurPaM.GetIndex(), aLine.getLength() );
        }
        if ( nEnd < aText.getLength() )
            aPaM = ImpInsertParaBreak( aPaM );

        nStart = nEnd+1;
    }

    UndoActionEnd();

    ESelection inserted{CreateEPaM(aCurPaM)};
    inserted.end = CreateEPaM(aPaM);
    UpdateSelectionsInsert(inserted);

    TextModified();
    return aPaM;
}

EditPaM ImpEditEngine::ImpFastInsertText( EditPaM aPaM, const OUString& rStr )
{
    OSL_ENSURE( rStr.indexOf( 0x0A ) == -1, "FastInsertText: Newline not allowed! ");
    OSL_ENSURE( rStr.indexOf( 0x0D ) == -1, "FastInsertText: Newline not allowed! ");
    OSL_ENSURE( rStr.indexOf( '\t' ) == -1, "FastInsertText: Newline not allowed! ");

    if ( ( aPaM.GetNode()->Len() + rStr.getLength() ) < MAXCHARSINPARA )
    {
        if ( IsUndoEnabled() && !IsInUndo() )
            InsertUndo(std::make_unique<EditUndoInsertChars>(mpEditEngine, CreateEPaM(aPaM), rStr));

        aPaM = maEditDoc.InsertText( aPaM, rStr );
        TextModified();
    }
    else
    {
        aPaM = ImpInsertText( EditSelection(aPaM), rStr );
    }

    return aPaM;
}

EditPaM ImpEditEngine::ImpInsertFeature(const EditSelection& rCurSel, const SfxPoolItem& rItem)
{
    EditPaM aPaM;
    if ( rCurSel.HasRange() )
        aPaM = ImpDeleteSelection( rCurSel );
    else
        aPaM = rCurSel.Max();

    if ( aPaM.GetIndex() >= SAL_MAX_INT32-1 )
        return aPaM;

    if ( IsUndoEnabled() && !IsInUndo() )
        InsertUndo(std::make_unique<EditUndoInsertFeature>(mpEditEngine, CreateEPaM(aPaM), rItem));
    aPaM = maEditDoc.InsertFeature( aPaM, rItem );
    UpdateFields();

    ParaPortion* pPortion = FindParaPortion( aPaM.GetNode() );
    assert(pPortion);
    pPortion->MarkInvalid( aPaM.GetIndex()-1, 1 );

    ESelection inserted{CreateEPaM(rCurSel.Min())};
    inserted.end = CreateEPaM(aPaM);
    UpdateSelectionsInsert(inserted);

    TextModified();

    return aPaM;
}

EditPaM ImpEditEngine::ImpInsertParaBreak( const EditSelection& rCurSel )
{
    EditPaM aPaM;
    if ( rCurSel.HasRange() )
        aPaM = ImpDeleteSelection( rCurSel );
    else
        aPaM = rCurSel.Max();

    return ImpInsertParaBreak( aPaM );
}

EditPaM ImpEditEngine::ImpInsertParaBreak( EditPaM& rPaM, bool bKeepEndingAttribs )
{
    if (maEditDoc.Count() >= EE_PARA_MAX)
    {
        SAL_WARN( "editeng", "ImpEditEngine::ImpInsertParaBreak - can't process more than "
                << EE_PARA_MAX << " paragraphs!");
        return rPaM;
    }

    if ( IsUndoEnabled() && !IsInUndo() )
        InsertUndo(std::make_unique<EditUndoSplitPara>(mpEditEngine, maEditDoc.GetPos(rPaM.GetNode()), rPaM.GetIndex()));

    EditPaM aPaM( maEditDoc.InsertParaBreak( rPaM, bKeepEndingAttribs ) );
    if (auto pStyle = aPaM.GetNode()->GetStyleSheet())
        StartListening(*pStyle, DuplicateHandling::Allow);

    if ( GetStatus().DoOnlineSpelling() )
    {
        sal_Int32 nEnd = rPaM.GetNode()->Len();
        aPaM.GetNode()->CreateWrongList();
        WrongList* pLWrongs = rPaM.GetNode()->GetWrongList();
        WrongList* pRWrongs = aPaM.GetNode()->GetWrongList();
        // take over misspelled words:
        for (auto & elem : *pLWrongs)
        {
            // Correct only if really a word gets overlapped in the process of
            // Spell checking
            if (elem.mnStart > o3tl::make_unsigned(nEnd))
            {
                pRWrongs->push_back(elem);
                editeng::MisspellRange& rRWrong = pRWrongs->back();
                rRWrong.mnStart = rRWrong.mnStart - nEnd;
                rRWrong.mnEnd = rRWrong.mnEnd - nEnd;
            }
            else if (elem.mnStart < o3tl::make_unsigned(nEnd) && elem.mnEnd > o3tl::make_unsigned(nEnd))
                elem.mnEnd = nEnd;
        }
        sal_Int32 nInv = nEnd ? nEnd-1 : nEnd;
        if ( nEnd )
            pLWrongs->SetInvalidRange(nInv, nEnd);
        else
            pLWrongs->SetValid();
        pRWrongs->SetValid();
        pRWrongs->SetInvalidRange(0, 1);  // Only test the first word
    }

    ParaPortion* pPortion = FindParaPortion( rPaM.GetNode() );
    assert(pPortion);
    pPortion->MarkInvalid( rPaM.GetIndex(), 0 );

    // Optimization: Do not place unnecessarily many getPos to Listen!
    // Here, as in undo, but also in all other methods.
    sal_Int32 nPos = GetParaPortions().GetPos( pPortion );
    assert(nPos != EE_PARA_MAX);
    ParaPortion* pNewPortion = new ParaPortion( aPaM.GetNode() );
    GetParaPortions().Insert(nPos+1, std::unique_ptr<ParaPortion>(pNewPortion));
    ParaAttribsChanged( pNewPortion->GetNode() );
    if ( IsCallParaInsertedOrDeleted() )
        GetEditEnginePtr()->ParagraphInserted( nPos+1 );

    if( nullptr != rPaM.GetNode() )
        rPaM.GetNode()->checkAndDeleteEmptyAttribs(); // if empty Attributes have emerged.

    ESelection inserted{CreateEPaM(rPaM)};
    inserted.end = CreateEPaM(aPaM);
    UpdateSelectionsInsert(inserted);

    TextModified();
    return aPaM;
}

EditPaM ImpEditEngine::ImpFastInsertParagraph( sal_Int32 nPara )
{
    if ( IsUndoEnabled() && !IsInUndo() )
    {
        if ( nPara )
        {
            assert(maEditDoc.GetObject(nPara - 1));
            InsertUndo(std::make_unique<EditUndoSplitPara>(mpEditEngine, nPara-1, maEditDoc.GetObject(nPara - 1)->Len()));
        }
        else
            InsertUndo(std::make_unique<EditUndoSplitPara>(mpEditEngine, 0, 0));
    }

    ContentNode* pNode = new ContentNode( maEditDoc.GetItemPool() );
    // If flat mode, then later no Font is set:
    pNode->GetCharAttribs().GetDefFont() = maEditDoc.GetDefFont();

    if ( GetStatus().DoOnlineSpelling() )
        pNode->CreateWrongList();

    maEditDoc.Insert(nPara, std::unique_ptr<ContentNode>(pNode));

    GetParaPortions().Insert(nPara, std::make_unique<ParaPortion>( pNode ));
    if ( IsCallParaInsertedOrDeleted() )
        GetEditEnginePtr()->ParagraphInserted( nPara );

    return EditPaM( pNode, 0 );
}

EditPaM ImpEditEngine::InsertParaBreak(const EditSelection& rCurSel)
{
    EditPaM aPaM(ImpInsertParaBreak(rCurSel));
    if ( maStatus.DoAutoIndenting() )
    {
        sal_Int32 nPara = maEditDoc.GetPos( aPaM.GetNode() );
        OSL_ENSURE( nPara > 0, "AutoIndenting: Error!" );
        const OUString aPrevParaText( GetEditDoc().GetParaAsString( nPara-1 ) );
        sal_Int32 n = 0;
        while ( ( n < aPrevParaText.getLength() ) &&
                ( ( aPrevParaText[n] == ' ' ) || ( aPrevParaText[n] == '\t' ) ) )
        {
            if ( aPrevParaText[n] == '\t' )
                aPaM = ImpInsertFeature( EditSelection(aPaM), SfxVoidItem( EE_FEATURE_TAB ) );
            else
                aPaM = ImpInsertText( EditSelection(aPaM), OUString(aPrevParaText[n]) );
            n++;
        }

    }
    return aPaM;
}

EditPaM ImpEditEngine::InsertTab(const EditSelection& rCurSel)
{
    EditPaM aPaM( ImpInsertFeature(rCurSel, SfxVoidItem(EE_FEATURE_TAB )));
    return aPaM;
}

EditPaM ImpEditEngine::InsertField(const EditSelection& rCurSel, const SvxFieldItem& rFld)
{
    return ImpInsertFeature(rCurSel, rFld);
}

bool ImpEditEngine::UpdateFields()
{
    bool bChanges = false;
    sal_Int32 nParas = GetEditDoc().Count();
    for ( sal_Int32 nPara = 0; nPara < nParas; nPara++ )
    {
        bool bChangesInPara = false;
        ContentNode* pNode = GetEditDoc().GetObject( nPara );
        assert(pNode);
        CharAttribList::AttribsType& rAttribs = pNode->GetCharAttribs().GetAttribs();
        for (std::unique_ptr<EditCharAttrib> & rAttrib : rAttribs)
        {
            EditCharAttrib& rAttr = *rAttrib;
            if (rAttr.Which() == EE_FEATURE_FIELD)
            {
                EditCharAttribField& rField = static_cast<EditCharAttribField&>(rAttr);
                EditCharAttribField aCurrent(rField);
                rField.Reset();

                if (!maStatus.MarkNonUrlFields() && !maStatus.MarkUrlFields())
                    ;   // nothing marked
                else if (maStatus.MarkNonUrlFields() && maStatus.MarkUrlFields())
                    rField.GetFieldColor() = GetColorConfig().GetColorValue(svtools::WRITERFIELDSHADINGS).nColor;
                else
                {
                    bool bURL = false;
                    if (const SvxFieldItem* pFieldItem = dynamic_cast<const SvxFieldItem*>(rField.GetItem()))
                    {
                        if (const SvxFieldData* pFieldData = pFieldItem->GetField())
                            bURL = (dynamic_cast<const SvxURLField* >(pFieldData) != nullptr);
                    }
                    if ((bURL && maStatus.MarkUrlFields()) || (!bURL && maStatus.MarkNonUrlFields()))
                        rField.GetFieldColor() = GetColorConfig().GetColorValue( svtools::WRITERFIELDSHADINGS ).nColor;
                }

                const OUString aFldValue =
                    GetEditEnginePtr()->CalcFieldValue(
                        static_cast<const SvxFieldItem&>(*rField.GetItem()),
                        nPara, rField.GetStart(), rField.GetTextColor(), rField.GetFieldColor(), rField.GetFldLineStyle() );

                rField.SetFieldValue(aFldValue);
                if (rField != aCurrent)
                {
                    bChanges = true;
                    bChangesInPara = true;
                }
            }
        }
        if ( bChangesInPara )
        {
            // If possible be more precise when invalidate.
            assert(GetParaPortions().exists(nPara));
            ParaPortion& rPortion = GetParaPortions().getRef(nPara);
            rPortion.MarkSelectionInvalid( 0 );
        }
    }
    return bChanges;
}

EditPaM ImpEditEngine::InsertLineBreak(const EditSelection& aCurSel)
{
    EditPaM aPaM( ImpInsertFeature( aCurSel, SfxVoidItem( EE_FEATURE_LINEBR ) ) );
    return aPaM;
}


//  Helper functions

tools::Rectangle ImpEditEngine::GetEditCursor(ParaPortion const& rPortion, EditLine const& rLine,
                                              sal_Int32 nIndex, CursorFlags aFlags)
{
    // nIndex might be not in the line
    // Search within the line...
    tools::Long nX;

    if (nIndex == rLine.GetStart() && aFlags.bStartOfLine)
    {
        Range aXRange = GetLineXPosStartEnd(rPortion, rLine);
        nX = !IsRightToLeft(GetEditDoc().GetPos(rPortion.GetNode())) ? aXRange.Min()
                                                                      : aXRange.Max();
    }
    else if (nIndex == rLine.GetEnd() && aFlags.bEndOfLine)
    {
        Range aXRange = GetLineXPosStartEnd(rPortion, rLine);
        nX = !IsRightToLeft(GetEditDoc().GetPos(rPortion.GetNode())) ? aXRange.Max()
                                                                      : aXRange.Min();
    }
    else
    {
        nX = GetXPos(rPortion, rLine, nIndex, aFlags.bPreferPortionStart);
    }

    tools::Rectangle aEditCursor;
    aEditCursor.SetLeft(nX);
    aEditCursor.SetRight(nX);

    aEditCursor.SetBottom(rLine.GetHeight() - 1);
    if (aFlags.bTextOnly)
        aEditCursor.SetTop(aEditCursor.Bottom() - rLine.GetTxtHeight() + 1);
    else
        aEditCursor.SetTop(aEditCursor.Bottom() - std::min(rLine.GetTxtHeight(), rLine.GetHeight()) + 1);
    return aEditCursor;
}

tools::Rectangle ImpEditEngine::PaMtoEditCursor( EditPaM aPaM, CursorFlags aFlags)
{
    assert( IsUpdateLayout() && "Must not be reached when Update=FALSE: PaMtoEditCursor" );

    tools::Rectangle aEditCursor;
    const sal_Int32 nIndex = aPaM.GetIndex();
    const ParaPortion* pPortion = nullptr;
    const EditLine* pLastLine = nullptr;
    tools::Rectangle aLineArea;

    auto FindPortionLineAndArea = [&, bEOL(aFlags.bEndOfLine)](const LineAreaInfo& rInfo)
    {
        if (!rInfo.pLine) // start of ParaPortion
        {
            ContentNode* pNode = rInfo.rPortion.GetNode();
            OSL_ENSURE(pNode, "Invalid Node in Portion!");
            if (pNode != aPaM.GetNode())
                return CallbackResult::SkipThisPortion;
            pPortion = &rInfo.rPortion;
        }
        else // guaranteed that this is the correct ParaPortion
        {
            pLastLine = rInfo.pLine;
            aLineArea = rInfo.aArea;
            if ((rInfo.pLine->GetStart() == nIndex) || (rInfo.pLine->IsIn(nIndex, bEOL)))
                return CallbackResult::Stop;
        }
        return CallbackResult::Continue;
    };
    IterateLineAreas(FindPortionLineAndArea, IterFlag::none);

    if (pLastLine && pPortion)
    {
        aEditCursor = GetEditCursor(*pPortion, *pLastLine, nIndex, aFlags);
        aEditCursor.Move(getTopLeftDocOffset(aLineArea));
    }
    else
        OSL_FAIL("Line not found!");

    return aEditCursor;
}

void ImpEditEngine::IterateLineAreas(const IterateLinesAreasFunc& f, IterFlag eOptions)
{
    const Point aOrigin(0, 0);
    Point aLineStart(aOrigin);
    const tools::Long nVertLineSpacing = CalcVertLineSpacing(aLineStart);
    const tools::Long nColumnWidth = GetColumnWidth(maPaperSize);
    sal_Int16 nColumn = 0;
    for (sal_Int32 n = 0, nPortions = GetParaPortions().Count(); n < nPortions; ++n)
    {
        ParaPortion& rPortion = GetParaPortions().getRef(n);
        bool bSkipThis = true;
        if (rPortion.IsVisible())
        {
            // when typing idle formatting, asynchronous Paint. Invisible Portions may be invalid.
            if (rPortion.IsInvalid())
                return;

            LineAreaInfo aInfo{
                rPortion,
                nullptr, // pLine
                0, // nHeightNeededToNotWrap
                { aLineStart, Size{ nColumnWidth, rPortion.GetFirstLineOffset() } }, // aArea
                n, // nPortion
                0, // nLine
                nColumn // nColumn
            };
            auto eResult = f(aInfo);
            if (eResult == CallbackResult::Stop)
                return;
            bSkipThis = eResult == CallbackResult::SkipThisPortion;

            sal_uInt16 nSBL = 0;
            if (!maStatus.IsOutliner())
            {
                const SvxLineSpacingItem& rLSItem
                    = rPortion.GetNode()->GetContentAttribs().GetItem(EE_PARA_SBL);
                nSBL = (rLSItem.GetInterLineSpaceRule() == SvxInterLineSpaceRule::Fix)
                           ? scaleYSpacingValue(rLSItem.GetInterLineSpace())
                           : 0;
            }

            adjustYDirectionAware(aLineStart, rPortion.GetFirstLineOffset());
            for (sal_Int32 nLine = 0, nLines = rPortion.GetLines().Count(); nLine < nLines; nLine++)
            {
                EditLine& rLine = rPortion.GetLines()[nLine];
                tools::Long nLineHeight = rLine.GetHeight();
                if (nLine != nLines - 1)
                    nLineHeight += nVertLineSpacing;
                MoveToNextLine(aLineStart, nLineHeight, nColumn, aOrigin,
                               &aInfo.nHeightNeededToNotWrap);
                const bool bInclILS = eOptions & IterFlag::inclILS;
                if (bInclILS && (nLine != nLines - 1) && !maStatus.IsOutliner())
                {
                    adjustYDirectionAware(aLineStart, nSBL);
                    nLineHeight += nSBL;
                }

                if (!bSkipThis)
                {
                    Point aOtherCorner(aLineStart);
                    adjustXDirectionAware(aOtherCorner, nColumnWidth);
                    adjustYDirectionAware(aOtherCorner, -nLineHeight);

                    // Calls to f() for each line
                    aInfo.nColumn = nColumn;
                    aInfo.pLine = &rLine;
                    aInfo.nLine = nLine;
                    aInfo.aArea = tools::Rectangle::Normalize(aLineStart, aOtherCorner);
                    eResult = f(aInfo);
                    if (eResult == CallbackResult::Stop)
                        return;
                    bSkipThis = eResult == CallbackResult::SkipThisPortion;
                }

                if (!bInclILS && (nLine != nLines - 1) && !maStatus.IsOutliner())
                    adjustYDirectionAware(aLineStart, nSBL);
            }
            if (!maStatus.IsOutliner())
            {
                const SvxULSpaceItem& rULItem = rPortion.GetNode()->GetContentAttribs().GetItem(EE_PARA_ULSPACE);
                tools::Long nUL = scaleYSpacingValue(rULItem.GetLower());
                adjustYDirectionAware(aLineStart, nUL);
            }
        }
        // Invisible ParaPortion has no height (see ParaPortion::GetHeight), don't handle it
    }
}

std::tuple<const ParaPortion*, const EditLine*, tools::Long>
ImpEditEngine::GetPortionAndLine(Point aDocPos)
{
    // First find the column from the point
    sal_Int32 nClickColumn = 0;
    for (tools::Long nColumnStart = 0, nColumnWidth = GetColumnWidth(maPaperSize);;
         nColumnStart += mnColumnSpacing + nColumnWidth, ++nClickColumn)
    {
        if (aDocPos.X() <= nColumnStart + nColumnWidth + mnColumnSpacing / 2)
            break;
        if (nClickColumn >= mnColumns - 1)
            break;
    }

    const ParaPortion* pLastPortion = nullptr;
    const EditLine* pLastLine = nullptr;
    tools::Long nLineStartX = 0;
    Point aPos;
    adjustYDirectionAware(aPos, aDocPos.Y());

    auto FindLastMatchingPortionAndLine = [&](const LineAreaInfo& rInfo) {
        if (rInfo.pLine) // Only handle lines, not ParaPortion starts
        {
            if (rInfo.nColumn > nClickColumn)
                return CallbackResult::Stop;
            pLastPortion = &rInfo.rPortion; // Candidate paragraph
            pLastLine = rInfo.pLine; // Last visible line not later than click position
            nLineStartX = getTopLeftDocOffset(rInfo.aArea).Width();
            if (rInfo.nColumn == nClickColumn && getYOverflowDirectionAware(aPos, rInfo.aArea) == 0)
                return CallbackResult::Stop; // Found it
        }
        return CallbackResult::Continue;
    };
    IterateLineAreas(FindLastMatchingPortionAndLine, IterFlag::inclILS);

    return { pLastPortion, pLastLine, nLineStartX };
}

EditPaM ImpEditEngine::GetPaM( Point aDocPos, bool bSmart )
{
    assert( IsUpdateLayout() && "Must not be reached when Update=FALSE: GetPaM" );

    if (const auto [pPortion, pLine, nLineStartX] = GetPortionAndLine(aDocPos); pPortion)
    {
        assert(pLine);
        assert(pPortion);
        sal_Int32 nCurIndex = GetChar(*pPortion, *pLine, aDocPos.X() - nLineStartX, bSmart);
        EditPaM aPaM(pPortion->GetNode(), nCurIndex);

        if (nCurIndex && (nCurIndex == pLine->GetEnd())
            && (pLine != &pPortion->GetLines()[pPortion->GetLines().Count() - 1]))
        {
            aPaM = CursorLeft(aPaM);
        }

        return aPaM;
    }
    return {};
}

bool ImpEditEngine::IsTextPos(const Point& rDocPos, sal_uInt16 nBorder)
{
    if (const auto [pPortion, pLine, nLineStartX] = GetPortionAndLine(rDocPos); pPortion)
    {
        assert(pLine);
        assert(pPortion);
        Range aLineXPosStartEnd = GetLineXPosStartEnd(*pPortion, *pLine);
        if ((rDocPos.X() >= nLineStartX + aLineXPosStartEnd.Min() - nBorder)
            && (rDocPos.X() <= nLineStartX + aLineXPosStartEnd.Max() + nBorder))
            return true;
    }
    return false;
}

sal_uInt32 ImpEditEngine::GetTextHeight() const
{
    assert( IsUpdateLayout() && "Should not be used for Update=FALSE: GetTextHeight" );
    OSL_ENSURE( IsFormatted() || IsFormatting(), "GetTextHeight: Not formatted" );
    return mnCurTextHeight;
}

sal_uInt32 ImpEditEngine::CalcTextWidth( bool bIgnoreExtraSpace )
{
    // If still not formatted and not in the process.
    // Will be brought in the formatting for AutoPageSize.
    if ( !IsFormatted() && !IsFormatting() )
        FormatDoc();

    sal_uInt32 nMaxWidth = 0;

    // Over all the paragraphs ...

    sal_Int32 nParas = GetParaPortions().Count();
    for ( sal_Int32 nPara = 0; nPara < nParas; nPara++ )
    {
        nMaxWidth = std::max(nMaxWidth, CalcParaWidth(nPara, bIgnoreExtraSpace));
    }

    return nMaxWidth;
}

sal_uInt32 ImpEditEngine::CalcParaWidth( sal_Int32 nPara, bool bIgnoreExtraSpace )
{
    // If still not formatted and not in the process.
    // Will be brought in the formatting for AutoPageSize.
    if ( !IsFormatted() && !IsFormatting() )
        FormatDoc();

    tools::Long nMaxWidth = 0;

    // Over all the paragraphs ...

    OSL_ENSURE(GetParaPortions().exists(nPara), "CalcParaWidth: Out of range");
    ParaPortion* pPortion = GetParaPortions().SafeGetObject(nPara);
    if ( pPortion && pPortion->IsVisible() )
    {
        const SvxLRSpaceItem& rLRItem = GetLRSpaceItem( pPortion->GetNode() );
        sal_Int32 nSpaceBeforeAndMinLabelWidth = GetSpaceBeforeAndMinLabelWidth( pPortion->GetNode() );

        auto stMetrics = GetFontUnitMetrics(pPortion->GetNode());

        // On the lines of the paragraph ...

        sal_Int32 nLines = pPortion->GetLines().Count();
        for ( sal_Int32 nLine = 0; nLine < nLines; nLine++ )
        {
            EditLine const& rLine = pPortion->GetLines()[nLine];
            // nCurWidth = pLine->GetStartPosX();
            // For Center- or Right- alignment it depends on the paper
            // width, here not preferred. I general, it is best not leave it
            // to StartPosX, also the right indents have to be taken into
            // account!
            tools::Long nCurWidth
                = scaleXSpacingValue(rLRItem.ResolveTextLeft({}) + nSpaceBeforeAndMinLabelWidth);
            if ( nLine == 0 )
            {
                tools::Long nFI = scaleXSpacingValue(rLRItem.ResolveTextFirstLineOffset(stMetrics));
                nCurWidth -= nFI;
                if ( pPortion->GetBulletX() > nCurWidth )
                {
                    nCurWidth += nFI;   // LI?
                    if ( pPortion->GetBulletX() > nCurWidth )
                        nCurWidth = pPortion->GetBulletX();
                }
            }
            nCurWidth += scaleXSpacingValue(rLRItem.ResolveRight({}));
            nCurWidth += CalcLineWidth(*pPortion, rLine, bIgnoreExtraSpace);
            if ( nCurWidth > nMaxWidth )
            {
                nMaxWidth = nCurWidth;
            }
        }
    }

    nMaxWidth++; // widen it, because in CreateLines for >= is wrapped.
    return static_cast<sal_uInt32>(nMaxWidth);
}

sal_uInt32 ImpEditEngine::CalcLineWidth(ParaPortion const& rPortion, EditLine const& rLine, bool bIgnoreExtraSpace)
{
    sal_Int32 nPara = GetEditDoc().GetPos(rPortion.GetNode());

    // #114278# Saving both layout mode and language (since I'm
    // potentially changing both)
    GetRefDevice()->Push( vcl::PushFlags::TEXTLAYOUTMODE|vcl::PushFlags::TEXTLANGUAGE );

    ImplInitLayoutMode(*GetRefDevice(), nPara, -1);

    SvxAdjust eJustification = GetJustification( nPara );

    // Calculation of the width without the Indents ...
    sal_uInt32 nWidth = 0;
    sal_Int32 nPos = rLine.GetStart();
    for ( sal_Int32 nTP = rLine.GetStartPortion(); nTP <= rLine.GetEndPortion(); nTP++ )
    {
        const TextPortion& rTextPortion = rPortion.GetTextPortions()[nTP];
        switch ( rTextPortion.GetKind() )
        {
            case PortionKind::FIELD:
            case PortionKind::HYPHENATOR:
            case PortionKind::TAB:
            {
                nWidth += rTextPortion.GetSize().Width();
            }
            break;
            case PortionKind::TEXT:
            {
                if ( ( eJustification != SvxAdjust::Block ) || ( !bIgnoreExtraSpace ) )
                {
                    nWidth += rTextPortion.GetSize().Width();
                }
                else
                {
                    SvxFont aTmpFont(rPortion.GetNode()->GetCharAttribs().GetDefFont());
                    SeekCursor(rPortion.GetNode(), nPos + 1, aTmpFont);
                    aTmpFont.SetPhysFont(*GetRefDevice());
                    ImplInitDigitMode(*GetRefDevice(), aTmpFont.GetLanguage());
                    nWidth += aTmpFont.QuickGetTextSize( GetRefDevice(),
                        rPortion.GetNode()->GetString(), nPos, rTextPortion.GetLen(), nullptr ).Width();
                }
            }
            break;
            case PortionKind::LINEBREAK: break;
        }
        nPos = nPos + rTextPortion.GetLen();
    }

    GetRefDevice()->Pop();

    return nWidth;
}

tools::Long ImpEditEngine::Calc1ColumnTextHeight()
{
    tools::Long nHeight = 0;
    // Pretend that we have ~infinite height to get total height
    comphelper::ValueRestorationGuard aGuard(mnCurTextHeight, std::numeric_limits<tools::Long>::max());

    IterateLinesAreasFunc FindLastLineBottom = [&](const LineAreaInfo& rInfo) {
        if (rInfo.pLine)
        {
            // bottom coordinate does not belong to area, so no need to do +1
            nHeight = getBottomDocOffset(rInfo.aArea);
        }
        return CallbackResult::Continue;
    };
    IterateLineAreas(FindLastLineBottom, IterFlag::none);
    return nHeight;
}

tools::Long ImpEditEngine::CalcTextHeight()
{
    assert( IsUpdateLayout() && "Should not be used when Update=FALSE: CalcTextHeight" );

    if (mnColumns <= 1)
        return Calc1ColumnTextHeight(); // All text fits into a single column - done!

    // The final column height can be smaller than total height divided by number of columns (taking
    // into account first line offset and interline spacing, that aren't considered in positioning
    // after the wrap). The wrap should only happen after the minimal height is exceeded.
    tools::Long nTentativeColHeight = mnMinColumnWrapHeight;
    tools::Long nWantedIncrease = 0;
    tools::Long nCurrentTextHeight;

    // This does the necessary column balancing for the case when the text does not fit min height.
    // When the height of column (taken from mnCurTextHeight) is too small, the last column will
    // overflow, so the resulting height of the text will exceed the set column height. Increasing
    // the column height step by step by the minimal value that allows one of columns to accommodate
    // one line more, we finally get to the point where all the text fits. At each iteration, the
    // height is only increased, so it's impossible to have infinite layout loops. The found value
    // is the global minimum.
    //
    // E.g., given the following four line heights:
    // Line 1: 10;
    // Line 2: 12;
    // Line 3: 10;
    // Line 4: 10;
    // number of columns 3, and the minimal paper height of 5, the iterations would be:
    // * Tentative column height is set to 5
    // <ITERATION 1>
    // * Line 1 is attempted to go to column 0. Overflow is 5 => moved to column 1.
    // * Line 2 is attempted to go to column 1 after Line 1; overflow is 17 => moved to column 2.
    // * Line 3 is attempted to go to column 2 after Line 2; overflow is 17, stays in max column 2.
    // * Line 4 goes to column 2 after Line 3.
    // * Final iteration columns are: {empty}, {Line 1}, {Line 2, Line 3, Line 4}
    // * Total text height is max({0, 10, 32}) == 32 > Tentative column height 5 => NEXT ITERATION
    // * Minimal height increase that allows at least one column to accommodate one more line is
    //   min({5, 17, 17}) = 5.
    // * Tentative column height is set to 5 + 5 = 10.
    // <ITERATION 2>
    // * Line 1 goes to column 0, no overflow.
    // * Line 2 is attempted to go to column 0 after Line 1; overflow is 12 => moved to column 1.
    // * Line 3 is attempted to go to column 1 after Line 2; overflow is 12 => moved to column 2.
    // * Line 4 is attempted to go to column 2 after Line 3; overflow is 10, stays in max column 2.
    // * Final iteration columns are: {Line 1}, {Line 2}, {Line 3, Line 4}
    // * Total text height is max({10, 12, 20}) == 20 > Tentative column height 10 => NEXT ITERATION
    // * Minimal height increase that allows at least one column to accommodate one more line is
    //   min({12, 12, 10}) = 10.
    // * Tentative column height is set to 10 + 10 == 20.
    // <ITERATION 3>
    // * Line 1 goes to column 0, no overflow.
    // * Line 2 is attempted to go to column 0 after Line 1; overflow is 2 => moved to column 1.
    // * Line 3 is attempted to go to column 1 after Line 2; overflow is 2 => moved to column 2.
    // * Line 4 is attempted to go to column 2 after Line 3; no overflow.
    // * Final iteration columns are: {Line 1}, {Line 2}, {Line 3, Line 4}
    // * Total text height is max({10, 12, 20}) == 20 == Tentative column height 20 => END.
    do
    {
        nTentativeColHeight += nWantedIncrease;
        nWantedIncrease = std::numeric_limits<tools::Long>::max();
        nCurrentTextHeight = 0;
        auto GetHeightAndWantedIncrease = [&, minHeight = tools::Long(0), lastCol = sal_Int16(0)](
                                              const LineAreaInfo& rInfo) mutable {
            if (rInfo.pLine)
            {
                if (lastCol != rInfo.nColumn)
                {
                    minHeight = std::max(nCurrentTextHeight,
                                    minHeight); // total height can't be less than previous columns
                    nWantedIncrease = std::min(rInfo.nHeightNeededToNotWrap, nWantedIncrease);
                    lastCol = rInfo.nColumn;
                }
                // bottom coordinate does not belong to area, so no need to do +1
                nCurrentTextHeight = std::max(getBottomDocOffset(rInfo.aArea), minHeight);
            }
            return CallbackResult::Continue;
        };
        comphelper::ValueRestorationGuard aGuard(mnCurTextHeight, nTentativeColHeight);
        IterateLineAreas(GetHeightAndWantedIncrease, IterFlag::none);
    } while (nCurrentTextHeight > nTentativeColHeight && nWantedIncrease > 0
             && nWantedIncrease != std::numeric_limits<tools::Long>::max());
    return nCurrentTextHeight;
}

sal_Int32 ImpEditEngine::GetLineCount( sal_Int32 nParagraph )
{
    if (!IsFormatted())
        FormatDoc();
    OSL_ENSURE(GetParaPortions().exists(nParagraph), "GetLineCount: Out of range");
    const ParaPortion* pPPortion = GetParaPortions().SafeGetObject(nParagraph);
    OSL_ENSURE( pPPortion, "Paragraph not found: GetLineCount" );
    if ( pPPortion )
        return pPPortion->GetLines().Count();

    return -1;
}

sal_Int32 ImpEditEngine::GetLineLen( sal_Int32 nParagraph, sal_Int32 nLine )
{
    if (!IsFormatted())
        FormatDoc();
    OSL_ENSURE(GetParaPortions().exists(nParagraph), "GetLineLen: Out of range");
    const ParaPortion* pPPortion = GetParaPortions().SafeGetObject(nParagraph);
    OSL_ENSURE(pPPortion, "Paragraph not found: GetLineLen");
    if ( pPPortion && ( nLine < pPPortion->GetLines().Count() ) )
    {
        const EditLine& rLine = pPPortion->GetLines()[nLine];
        return rLine.GetLen();
    }

    return -1;
}

void ImpEditEngine::GetLineBoundaries( /*out*/sal_Int32 &rStart, /*out*/sal_Int32 &rEnd, sal_Int32 nParagraph, sal_Int32 nLine )
{
    if (!IsFormatted())
        FormatDoc();
    OSL_ENSURE(GetParaPortions().exists(nParagraph), "GetLineCount: Out of range");
    const ParaPortion* pPPortion = GetParaPortions().SafeGetObject( nParagraph );
    OSL_ENSURE( pPPortion, "Paragraph not found: GetLineBoundaries" );
    rStart = rEnd = -1;     // default values in case of error
    if ( pPPortion && ( nLine < pPPortion->GetLines().Count() ) )
    {
        const EditLine& rLine = pPPortion->GetLines()[nLine];
        rStart = rLine.GetStart();
        rEnd   = rLine.GetEnd();
    }
}

sal_Int32 ImpEditEngine::GetLineNumberAtIndex( sal_Int32 nPara, sal_Int32 nIndex )
{
    if (!IsFormatted())
        FormatDoc();
    const ContentNode* pNode = GetEditDoc().GetObject( nPara );
    OSL_ENSURE( pNode, "GetLineNumberAtIndex: invalid paragraph index" );
    if (!pNode)
        return -1;
    // we explicitly allow for the index to point at the character right behind the text
    const bool bValidIndex = /*0 <= nIndex &&*/ nIndex <= pNode->Len();
    OSL_ENSURE( bValidIndex, "GetLineNumberAtIndex: invalid index" );
    const ParaPortion* pPPortion = maParaPortionList.SafeGetObject(nPara);
    if (!pPPortion)
    {
        SAL_WARN( "editeng", "ImpEditEngine::GetLineNumberAtIndex missing ParaPortion");
        return -1;
    }
    const EditLineList& rLineList = pPPortion->GetLines();
    const sal_Int32 nLineCount = rLineList.Count();
    sal_Int32 nLineNo = -1;
    if (nIndex == pNode->Len())
        nLineNo = nLineCount > 0 ? nLineCount - 1 : 0;
    else if (bValidIndex)   // nIndex < pNode->Len()
    {
        sal_Int32 nStart = -1, nEnd = -1;
        for (sal_Int32 i = 0;  i < nLineCount && nLineNo == -1;  ++i)
        {
            const EditLine& rLine = rLineList[i];
            nStart = rLine.GetStart();
            nEnd   = rLine.GetEnd();
            if (nStart >= 0 && nStart <= nIndex && nEnd >= 0 && nIndex < nEnd)
                nLineNo = i;
        }
    }
    return nLineNo;
}

sal_uInt16 ImpEditEngine::GetLineHeight( sal_Int32 nParagraph, sal_Int32 nLine )
{
    if (!IsFormatted())
        FormatDoc();
    OSL_ENSURE(GetParaPortions().exists(nParagraph), "GetLineCount: Out of range");
    ParaPortion* pPPortion = GetParaPortions().SafeGetObject( nParagraph );
    OSL_ENSURE( pPPortion, "Paragraph not found: GetLineHeight" );
    if ( pPPortion && ( nLine < pPPortion->GetLines().Count() ) )
    {
        const EditLine& rLine = pPPortion->GetLines()[nLine];
        return rLine.GetHeight();
    }

    return 0xFFFF;
}

tools::Rectangle ImpEditEngine::GetParaBounds( sal_Int32 nPara )
{
    if (!IsFormatted())
        FormatDoc();
    Point aPnt = GetDocPosTopLeft( nPara );

    if( IsEffectivelyVertical() )
    {
        sal_Int32 nTextHeight = GetTextHeight();
        sal_Int32 nParaWidth = CalcParaWidth(nPara, true);
        sal_Int32 nParaHeight = GetParaHeight(nPara);

        return tools::Rectangle( nTextHeight - aPnt.Y() - nParaHeight, 0, nTextHeight - aPnt.Y(), nParaWidth );
    }
    else
    {
        sal_Int32 nParaWidth = CalcParaWidth( nPara, true );
        sal_Int32 nParaHeight = GetParaHeight( nPara );

        return tools::Rectangle( 0, aPnt.Y(), nParaWidth, aPnt.Y() + nParaHeight );
    }
}

Point ImpEditEngine::GetDocPosTopLeft( sal_Int32 nParagraph )
{
    const ParaPortion* pPPortion = maParaPortionList.SafeGetObject(nParagraph);
    DBG_ASSERT( pPPortion, "Paragraph not found: GetWindowPosTopLeft" );

    Point aPoint;
    if ( pPPortion )
    {
        auto stMetrics = GetFontUnitMetrics(pPPortion->GetNode());

        // If someone calls GetLineHeight() with an empty Engine.
        DBG_ASSERT(IsFormatted() || !IsFormatting(), "GetDocPosTopLeft: Doc not formatted - unable to format!");
        if (!IsFormatted())
            FormatAndLayout();
        if (pPPortion->GetLines().Count())
        {
            // Correct it if large Bullet.
            const EditLine& rFirstLine = pPPortion->GetLines()[0];
            aPoint.setX( rFirstLine.GetStartPosX() );
        }
        else
        {
            const SvxLRSpaceItem& rLRItem = GetLRSpaceItem(pPPortion->GetNode());
            sal_Int32 nSpaceBefore = 0;
            GetSpaceBeforeAndMinLabelWidth(pPPortion->GetNode(), &nSpaceBefore);
            short nX = static_cast<short>(rLRItem.ResolveTextLeft(stMetrics)
                                          + rLRItem.ResolveTextFirstLineOffset(stMetrics)
                                          + nSpaceBefore);

            aPoint.setX(scaleXSpacingValue(nX));
        }
        aPoint.setY(maParaPortionList.GetYOffset(pPPortion));
    }
    return aPoint;
}

sal_uInt32 ImpEditEngine::GetParaHeight(sal_Int32 nParagraph) const
{
    sal_uInt32 nHeight = 0;

    ParaPortion const* pPPortion = GetParaPortions().SafeGetObject( nParagraph );
    OSL_ENSURE( pPPortion, "Paragraph not found: GetParaHeight" );

    if ( pPPortion )
        nHeight = pPPortion->GetHeight();

    return nHeight;
}

bool ImpEditEngine::UpdateSelection(EditSelection & rCurSel)
{
    bool bChanged = false;
    for (const std::unique_ptr<DeletedNodeInfo> & aDeletedNode : maDeletedNodes)
    {
        const DeletedNodeInfo& rInf = *aDeletedNode;
        if ((rCurSel.Min().GetNode() == rInf.GetNode()) ||
            (rCurSel.Max().GetNode() == rInf.GetNode()))
        {
            // Use ParaPortions, as now also hidden paragraphs have to be
            // taken into account!
            sal_Int32 nPara = rInf.GetPosition();
            if (!GetParaPortions().exists(nPara)) // Last paragraph
            {
                nPara = GetParaPortions().lastIndex();
            }
            assert(GetParaPortions().exists(nPara) && "Empty Document in UpdateSelections ?");
            // Do not end up from a hidden paragraph:
            sal_Int32 nCurrentPara = nPara;
            sal_Int32 nLastParaIndex = GetParaPortions().lastIndex();
            while (nPara <= nLastParaIndex && !GetParaPortions().getRef(nPara).IsVisible())
                nPara++;
            if (nPara > nLastParaIndex) // then also backwards ...
            {
                nPara = nCurrentPara;
                while ( nPara && !GetParaPortions().getRef(nPara).IsVisible() )
                    nPara--;
            }
            OSL_ENSURE(GetParaPortions().getRef(nPara).IsVisible(), "No visible paragraph found: UpdateSelections" );

            ParaPortion& rParaPortion = GetParaPortions().getRef(nPara);
            EditSelection aTmpSelection(EditPaM(rParaPortion.GetNode(), 0));
            rCurSel = aTmpSelection;
            bChanged=true;
            break;  // for loop
        }
    }
    if ( !bChanged )
    {
        // Check Index if node shrunk.
        if (rCurSel.Min().GetIndex() > rCurSel.Min().GetNode()->Len())
        {
            rCurSel.Min().SetIndex(rCurSel.Min().GetNode()->Len());
            bChanged = true;
        }
        if (rCurSel.Max().GetIndex() > rCurSel.Max().GetNode()->Len())
        {
            rCurSel.Max().SetIndex(rCurSel.Max().GetNode()->Len());
            bChanged = true;
        }
    }
    return bChanged;
}

void ImpEditEngine::UpdateSelections()
{
    // Check whether one of the selections is at a deleted node...
    // If the node is valid, the index has yet to be examined!
    for (EditView* pView : maEditViews)
    {
        EditSelection aCurSel( pView->getImpl().GetEditSelection() );
        if (UpdateSelection(aCurSel))
        {
            pView->getImpl().SetEditSelection(aCurSel);
        }
#if ENABLE_YRS
        for (auto & it : pView->getImpl().m_PeerCursors)
        {
            EditSelection sel(CreateSel(it.second.second));
            if (UpdateSelection(sel))
            {
                assert(false); // should have called Insert/Delete?
                it.second.second = CreateESel(sel);
            }
        }
#endif
    }
    maDeletedNodes.clear();
}

#if ENABLE_YRS
static bool UpdatePosDelete(EPaM & rPos, ESelection const& rDeleted)
{
    // correct towards start: RemoveParagraph(Count()) can't work otherwise
    assert(rDeleted.IsAdjusted());
    if (rDeleted.start.nPara == rPos.nPara)
    {
        if (rDeleted.start.nIndex < rPos.nIndex)
        {
            if (rDeleted.start.nPara == rDeleted.end.nPara && rDeleted.end.nIndex < rPos.nIndex)
            {
                rPos.nIndex -= (rDeleted.end.nIndex - rDeleted.start.nIndex);
            }
            else
            {
                rPos.nIndex = rDeleted.start.nIndex;
            }
            return true;
        }
    }
    else if (rDeleted.start.nPara < rPos.nPara && rPos.nPara <= rDeleted.end.nPara)
    {
        if (rPos.nPara == rDeleted.end.nPara && rDeleted.end.nIndex < rPos.nIndex)
        {
            rPos.nIndex -= rDeleted.end.nIndex - rDeleted.start.nIndex;
        }
        else
        {
            rPos.nIndex = rDeleted.start.nIndex;
        }
        rPos.nPara = rDeleted.start.nPara;
        return true;
    }
    else if (rDeleted.end.nPara < rPos.nPara)
    {
        rPos.nPara -= rDeleted.end.nPara - rDeleted.start.nPara;
        return true;
    }
    return false;
}
#endif

static bool UpdatePosInsert(EPaM & rPos, ESelection const& rInserted)
{
    assert(rInserted.IsAdjusted());
    if (rInserted.start.nPara == rPos.nPara)
    {
        if (rInserted.start.nIndex <= rPos.nIndex)
        {
            rPos.nPara = rInserted.end.nPara;
            rPos.nIndex = rInserted.end.nIndex + (rPos.nIndex - rInserted.start.nIndex);
            return true;
        }
    }
    else if (rInserted.start.nPara < rPos.nPara && rInserted.start.nPara != rInserted.end.nPara)
    {
        rPos.nPara += rInserted.end.nPara - rInserted.start.nPara;
        return true;
    }
    return false;
}

void ImpEditEngine::UpdateSelectionsDelete(ESelection const& rDeleted)
{
    ESelection deleted(rDeleted);
    deleted.Adjust();
    for (EditView *const pView : maEditViews)
    {
        // FIXME fails because CreateESel needs the deleted nodes! but if it's
        // called before deleting the nodes, it will assign wrong nodes...
        // perhaps the cursors could be stored as ESelection in the first
        // place, but that would require reviewing all code that inserts or
        // deletes nodes if it does the update in the correct place...
#if 0
        EditSelection const sel{pView->getImpl().GetEditSelection()};
        ESelection esel{CreateESel(sel)};
        bool isChanged{false};
        isChanged |= UpdatePosDelete(esel.start, deleted);
        isChanged |= UpdatePosDelete(esel.end, deleted);
        if (isChanged)
        {
            pView->getImpl().SetEditSelection(CreateSel(esel));
        }
#else
        (void)pView;
        (void)deleted;
#endif
#if ENABLE_YRS
        for (auto & it : pView->getImpl().m_PeerCursors)
        {
            UpdatePosDelete(it.second.second.start, deleted);
            UpdatePosDelete(it.second.second.end, deleted);
        }
#endif
    }
}

void ImpEditEngine::UpdateSelectionsInsert(ESelection const& rInserted)
{
    for (EditView *const pView : maEditViews)
    {
        EditSelection const sel{pView->getImpl().GetEditSelection()};
        ESelection esel{CreateESel(sel)};
        // nodes have been inserted, but CreateESel counts them: adjust for this!
        if (rInserted.start.nPara < esel.start.nPara)
        {
            esel.start.nPara -= rInserted.end.nPara - rInserted.start.nPara;
        }
        if (rInserted.start.nPara < esel.end.nPara)
        {
            esel.end.nPara -= rInserted.end.nPara - rInserted.start.nPara;
        }
        bool isChanged{false};
        isChanged |= UpdatePosInsert(esel.start, rInserted);
        isChanged |= UpdatePosInsert(esel.end, rInserted);
        if (isChanged)
        {
            pView->getImpl().SetEditSelection(CreateSel(esel));
        }
#if ENABLE_YRS
        for (auto & it : pView->getImpl().m_PeerCursors)
        {
            UpdatePosInsert(it.second.second.start, rInserted);
            UpdatePosInsert(it.second.second.end, rInserted);
        }
#endif
    }
}

EditSelection ImpEditEngine::ConvertSelection(
    sal_Int32 nStartPara, sal_Int32 nStartPos, sal_Int32 nEndPara, sal_Int32 nEndPos )
{
    EditSelection aNewSelection;

    // Start...
    ContentNode* pNode = maEditDoc.GetObject( nStartPara );
    sal_Int32 nIndex = nStartPos;
    if ( !pNode )
    {
        pNode = maEditDoc.GetObject(maEditDoc.Count() - 1);
        nIndex = pNode->Len();
    }
    else if ( nIndex > pNode->Len() )
        nIndex = pNode->Len();

    aNewSelection.Min().SetNode( pNode );
    aNewSelection.Min().SetIndex( nIndex );

    // End...
    pNode = maEditDoc.GetObject( nEndPara );
    nIndex = nEndPos;
    if ( !pNode )
    {
        pNode = maEditDoc.GetObject(maEditDoc.Count() - 1);
        nIndex = pNode->Len();
    }
    else if ( nIndex > pNode->Len() )
        nIndex = pNode->Len();

    aNewSelection.Max().SetNode( pNode );
    aNewSelection.Max().SetIndex( nIndex );

    return aNewSelection;
}

void ImpEditEngine::SetActiveView( EditView* pView )
{
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // Actually, now bHasVisSel and HideSelection would be necessary     !!!

    if (pView == mpActiveView)
        return;

    if (mpActiveView && mpActiveView->HasSelection())
        mpActiveView->getImpl().DrawSelectionXOR();

    mpActiveView = pView;

    if (mpActiveView && mpActiveView->HasSelection())
        mpActiveView->getImpl().DrawSelectionXOR();

    //  NN: Quick fix for #78668#:
    //  When editing of a cell in Calc is ended, the edit engine is not deleted,
    //  only the edit views are removed. If mpIMEInfos is still set in that case,
    //  mpIMEInfos->aPos points to an invalid selection.
    //  -> reset mpIMEInfos now
    //  (probably something like this is necessary whenever the content is modified
    //  from the outside)

    if ( !pView && mpIMEInfos )
    {
        mpIMEInfos.reset();
    }
}

uno::Reference< datatransfer::XTransferable > ImpEditEngine::CreateTransferable( const EditSelection& rSelection )
{
    EditSelection aSelection( rSelection );
    aSelection.Adjust( GetEditDoc() );

    rtl::Reference<EditDataObject> pDataObj = new EditDataObject;

    pDataObj->GetString() = convertLineEnd(GetSelected(aSelection), GetSystemLineEnd()); // System specific

    WriteRTF( pDataObj->GetRTFStream(), aSelection, /*bClipboard=*/true );
    pDataObj->GetRTFStream().Seek( 0 );

    WriteXML( pDataObj->GetODFStream(), aSelection );
    pDataObj->GetODFStream().Seek( 0 );

    //Dumping the ODFStream to a XML file for testing purpose
    /*
    std::filebuf afilebuf;
    afilebuf.open ("gsoc17_clipboard_test.xml",std::ios::out);
    std::ostream os(&afilebuf);
    os.write((const char*)(pDataObj->GetODFStream().GetData()), pDataObj->GetODFStream().remainingSize());
    afilebuf.close();
    */
    //dumping ends

    if ( ( aSelection.Min().GetNode() == aSelection.Max().GetNode() )
            && ( aSelection.Max().GetIndex() == (aSelection.Min().GetIndex()+1) ) )
    {
        const EditCharAttrib* pAttr = aSelection.Min().GetNode()->GetCharAttribs().
            FindFeature( aSelection.Min().GetIndex() );
        if ( pAttr &&
            ( pAttr->GetStart() == aSelection.Min().GetIndex() ) &&
            ( pAttr->Which() == EE_FEATURE_FIELD ) )
        {
            const SvxFieldItem* pField = static_cast<const SvxFieldItem*>(pAttr->GetItem());
            const SvxFieldData* pFld = pField->GetField();
            if ( auto pUrlField = dynamic_cast<const SvxURLField* >(pFld) )
            {
                // Office-Bookmark
                pDataObj->GetURL() = pUrlField->GetURL();
            }
        }
    }

    return pDataObj;
}

EditSelection ImpEditEngine::PasteText( uno::Reference< datatransfer::XTransferable > const & rxDataObj, const OUString& rBaseURL, const EditPaM& rPaM, bool bUseSpecial, SotClipboardFormatId format)
{
    EditSelection aNewSelection( rPaM );

    if ( !rxDataObj.is() )
        return aNewSelection;

    datatransfer::DataFlavor aFlavor;
    bool bDone = false;

    if ( bUseSpecial )
    {
        // XML
        SotExchange::GetFormatDataFlavor( SotClipboardFormatId::EDITENGINE_ODF_TEXT_FLAT, aFlavor );
        if ( rxDataObj->isDataFlavorSupported( aFlavor ) && (SotClipboardFormatId::NONE == format || SotClipboardFormatId::EDITENGINE_ODF_TEXT_FLAT == format))
        {
            try
            {
                uno::Any aData = rxDataObj->getTransferData( aFlavor );
                uno::Sequence< sal_Int8 > aSeq;
                aData >>= aSeq;
                {
                    SvMemoryStream aODFStream( aSeq.getArray(), aSeq.getLength(), StreamMode::READ );
                    aNewSelection = Read( aODFStream, rBaseURL, EETextFormat::Xml, EditSelection(rPaM) );
                }
                bDone = true;
            }
            catch( const css::uno::Exception&)
            {
                TOOLS_WARN_EXCEPTION( "editeng", "Unable to paste EDITENGINE_ODF_TEXT_FLAT" );
            }
        }

        if (!bDone) {
            // HTML_SIMPLE
            SotExchange::GetFormatDataFlavor(SotClipboardFormatId::HTML_SIMPLE, aFlavor);
            bool bHtmlSupported = rxDataObj->isDataFlavorSupported(aFlavor);
            if (bHtmlSupported && (SotClipboardFormatId::NONE == format || SotClipboardFormatId::HTML_SIMPLE == format)) {
                MSE40HTMLClipFormatObj aMSE40HTMLClipFormatObj;
                try
                {
                    uno::Any aData = rxDataObj->getTransferData(aFlavor);
                    uno::Sequence< sal_Int8 > aSeq;
                    aData >>= aSeq;
                    {
                        SvMemoryStream aHtmlStream(aSeq.getArray(), aSeq.getLength(), StreamMode::READ);
                        SvStream* pHtmlStream = aMSE40HTMLClipFormatObj.IsValid(aHtmlStream);
                        if (pHtmlStream != nullptr) {
                            aNewSelection = Read(*pHtmlStream, rBaseURL, EETextFormat::Html, EditSelection(rPaM));
                        }
                    }
                    bDone = true;
                }
                catch (const css::uno::Exception&)
                {
                }
            }
        }

        if ( !bDone )
        {
            // RTF
            SotExchange::GetFormatDataFlavor( SotClipboardFormatId::RTF, aFlavor );
            // RICHTEXT
            datatransfer::DataFlavor aFlavorRichtext;
            SotExchange::GetFormatDataFlavor( SotClipboardFormatId::RICHTEXT, aFlavorRichtext );
            bool bRtfSupported = rxDataObj->isDataFlavorSupported( aFlavor );
            bool bRichtextSupported  = rxDataObj->isDataFlavorSupported( aFlavorRichtext );
            if ( (bRtfSupported || bRichtextSupported) && (SotClipboardFormatId::NONE == format || SotClipboardFormatId::RICHTEXT == format || SotClipboardFormatId::RTF == format))
            {
                if(bRichtextSupported)
                {
                    aFlavor = std::move(aFlavorRichtext);
                }
                try
                {
                    uno::Any aData = rxDataObj->getTransferData( aFlavor );
                    uno::Sequence< sal_Int8 > aSeq;
                    aData >>= aSeq;
                    {
                        SvMemoryStream aRTFStream( aSeq.getArray(), aSeq.getLength(), StreamMode::READ );
                        aNewSelection = Read( aRTFStream, rBaseURL, EETextFormat::Rtf, EditSelection(rPaM) );
                    }
                    bDone = true;
                }
                catch( const css::uno::Exception& )
                {
                }
            }
        }

        if (!bDone)
        {
            // HTML
            SotExchange::GetFormatDataFlavor(SotClipboardFormatId::HTML, aFlavor);
            bool bHtmlSupported = rxDataObj->isDataFlavorSupported(aFlavor);
            if (bHtmlSupported
                && (format == SotClipboardFormatId::NONE || format == SotClipboardFormatId::HTML))
            {
                try
                {
                    uno::Any aData = rxDataObj->getTransferData(aFlavor);
                    uno::Sequence<sal_Int8> aSeq;
                    aData >>= aSeq;
                    SvMemoryStream aHtmlStream(aSeq.getArray(), aSeq.getLength(), StreamMode::READ);
                    aNewSelection = Read(aHtmlStream, rBaseURL, EETextFormat::Html, EditSelection(rPaM));
                    bDone = true;
                }
                catch (const css::uno::Exception&)
                {
                    TOOLS_WARN_EXCEPTION("editeng", "HTML paste failed");
                }
            }
        }
    }
    if ( !bDone )
    {
        SotExchange::GetFormatDataFlavor( SotClipboardFormatId::STRING, aFlavor );
        if ( rxDataObj->isDataFlavorSupported( aFlavor ) )
        {
            try
            {
                uno::Any aData = rxDataObj->getTransferData( aFlavor );
                OUString aText;
                aData >>= aText;
                aNewSelection = ImpInsertText( EditSelection(rPaM), aText );
            }
            catch( ... )
            {
                ; // #i9286# can happen, even if isDataFlavorSupported returns true...
            }
        }
    }

    return aNewSelection;
}

sal_Int32 ImpEditEngine::GetChar(ParaPortion const& rParaPortion, EditLine const& rLine, tools::Long nXPos, bool bSmart)
{
    sal_Int32 nChar = -1;
    sal_Int32 nCurIndex = rLine.GetStart();


    // Search best matching portion with GetPortionXOffset()
    for ( sal_Int32 i = rLine.GetStartPortion(); i <= rLine.GetEndPortion(); i++ )
    {
        const TextPortion& rPortion = rParaPortion.GetTextPortions()[i];
        tools::Long nXLeft = GetPortionXOffset(rParaPortion, rLine, i);
        tools::Long nXRight = nXLeft + rPortion.GetSize().Width();
        if ( ( nXLeft <= nXPos ) && ( nXRight >= nXPos ) )
        {
            nChar = nCurIndex;

            // Search within Portion...

            // Don't search within special portions...
            if ( rPortion.GetKind() != PortionKind::TEXT )
            {
                // ...but check on which side
                if ( bSmart )
                {
                    tools::Long nLeftDiff = nXPos-nXLeft;
                    tools::Long nRightDiff = nXRight-nXPos;
                    if ( nRightDiff < nLeftDiff )
                        nChar++;
                }
            }
            else
            {
                sal_Int32 nMax = rPortion.GetLen();
                sal_Int32 nOffset = -1;
                sal_Int32 nTmpCurIndex = nChar - rLine.GetStart();

                tools::Long nXInPortion = nXPos - nXLeft;
                if ( rPortion.IsRightToLeft() )
                    nXInPortion = nXRight - nXPos;

                // Search in Array...
                for ( sal_Int32 x = 0; x < nMax; x++ )
                {
                    tools::Long nTmpPosMax = rLine.GetCharPosArray()[nTmpCurIndex+x];
                    if ( nTmpPosMax > nXInPortion )
                    {
                        // Check whether this or the previous...
                        tools::Long nTmpPosMin = x ? rLine.GetCharPosArray()[nTmpCurIndex+x-1] : 0;
                        tools::Long nDiffLeft = nXInPortion - nTmpPosMin;
                        tools::Long nDiffRight = nTmpPosMax - nXInPortion;
                        OSL_ENSURE( nDiffLeft >= 0, "DiffLeft negative" );
                        OSL_ENSURE( nDiffRight >= 0, "DiffRight negative" );

                        if (bSmart && nDiffRight < nDiffLeft)
                        {
                            // I18N: If there are character position with the length of 0,
                            // they belong to the same character, we can not use this position as an index.
                            // Skip all 0-positions, cheaper than using XBreakIterator:
                            tools::Long nX = rLine.GetCharPosArray()[nTmpCurIndex + x];
                            while(x < nMax && static_cast<tools::Long>(rLine.GetCharPosArray()[nTmpCurIndex + x]) == nX)
                                ++x;
                        }
                        nOffset = x;
                        break;
                    }
                }

                // There should not be any inaccuracies when using the
                // CharPosArray! Maybe for kerning?
                // 0xFFF happens for example for Outline-Font when at the very end.
                if ( nOffset < 0 )
                    nOffset = nMax;

                OSL_ENSURE( nOffset <= nMax, "nOffset > nMax" );

                nChar = nChar + nOffset;

                // Check if index is within a cell:
                if ( nChar && ( nChar < rParaPortion.GetNode()->Len() ) )
                {
                    EditPaM aPaM( rParaPortion.GetNode(), nChar+1 );
                    sal_uInt16 nScriptType = GetI18NScriptType( aPaM );
                    if ( nScriptType == i18n::ScriptType::COMPLEX )
                    {
                        uno::Reference < i18n::XBreakIterator > _xBI( ImplGetBreakIterator() );
                        sal_Int32 nCount = 1;
                        lang::Locale aLocale = GetLocale( aPaM );
                        sal_Int32 nRight = _xBI->nextCharacters(
                            rParaPortion.GetNode()->GetString(), nChar, aLocale, css::i18n::CharacterIteratorMode::SKIPCELL, nCount, nCount );
                        sal_Int32 nLeft = _xBI->previousCharacters(
                            rParaPortion.GetNode()->GetString(), nRight, aLocale, css::i18n::CharacterIteratorMode::SKIPCELL, nCount, nCount );
                        if ( ( nLeft != nChar ) && ( nRight != nChar ) )
                        {
                            nChar = ( std::abs( nRight - nChar ) < std::abs( nLeft - nChar ) ) ? nRight : nLeft;
                        }
                    }
                    else
                    {
                        OUString aStr(rParaPortion.GetNode()->GetString());
                        // tdf#102625: don't select middle of a pair of surrogates with mouse cursor
                        if (rtl::isSurrogate(aStr[nChar]))
                            --nChar;
                    }
                }
            }
        }

        nCurIndex = nCurIndex + rPortion.GetLen();
    }

    if ( nChar == -1 )
    {
        nChar = ( nXPos <= rLine.GetStartPosX() ) ? rLine.GetStart() : rLine.GetEnd();
    }

    return nChar;
}

Range ImpEditEngine::GetLineXPosStartEnd(ParaPortion const& rParaPortion, EditLine const& rLine) const
{
    Range aLineXPosStartEnd;

    sal_Int32 nPara = GetEditDoc().GetPos(rParaPortion.GetNode());
    if ( !IsRightToLeft( nPara ) )
    {
        aLineXPosStartEnd.Min() = rLine.GetStartPosX();
        aLineXPosStartEnd.Max() = rLine.GetStartPosX() + rLine.GetTextWidth();
    }
    else
    {
        aLineXPosStartEnd.Min() = GetPaperSize().Width() - (rLine.GetStartPosX() + rLine.GetTextWidth());
        aLineXPosStartEnd.Max() = GetPaperSize().Width() - rLine.GetStartPosX();
    }

    return aLineXPosStartEnd;
}

tools::Long ImpEditEngine::GetPortionXOffset(ParaPortion const& rParaPortion, EditLine const& rLine, sal_Int32 nTextPortion) const
{
    tools::Long nX = rLine.GetStartPosX();

    for ( sal_Int32 i = rLine.GetStartPortion(); i < nTextPortion; i++ )
    {
        const TextPortion& rPortion = rParaPortion.GetTextPortions()[i];
        switch ( rPortion.GetKind() )
        {
            case PortionKind::FIELD:
            case PortionKind::TEXT:
            case PortionKind::HYPHENATOR:
            case PortionKind::TAB:
            {
                nX += rPortion.GetSize().Width();
            }
            break;
            case PortionKind::LINEBREAK: break;
        }
    }

    sal_Int32 nPara = GetEditDoc().GetPos(rParaPortion.GetNode());
    bool bR2LPara = IsRightToLeft( nPara );

    const TextPortion& rDestPortion = rParaPortion.GetTextPortions()[nTextPortion];
    if ( rDestPortion.GetKind() != PortionKind::TAB )
    {
        if ( !bR2LPara && rDestPortion.GetRightToLeftLevel() )
        {
            // Portions behind must be added, visual before this portion
            sal_Int32 nTmpPortion = nTextPortion+1;
            while ( nTmpPortion <= rLine.GetEndPortion() )
            {
                const TextPortion& rNextTextPortion = rParaPortion.GetTextPortions()[nTmpPortion];
                if ( rNextTextPortion.GetRightToLeftLevel() && ( rNextTextPortion.GetKind() != PortionKind::TAB ) )
                    nX += rNextTextPortion.GetSize().Width();
                else
                    break;
                nTmpPortion++;
            }
            // Portions before must be removed, visual behind this portion
            nTmpPortion = nTextPortion;
            while ( nTmpPortion > rLine.GetStartPortion() )
            {
                --nTmpPortion;
                const TextPortion& rPrevTextPortion = rParaPortion.GetTextPortions()[nTmpPortion];
                if ( rPrevTextPortion.GetRightToLeftLevel() && ( rPrevTextPortion.GetKind() != PortionKind::TAB ) )
                    nX -= rPrevTextPortion.GetSize().Width();
                else
                    break;
            }
        }
        else if ( bR2LPara && !rDestPortion.IsRightToLeft() )
        {
            // Portions behind must be removed, visual behind this portion
            sal_Int32 nTmpPortion = nTextPortion+1;
            while ( nTmpPortion <= rLine.GetEndPortion() )
            {
                const TextPortion& rNextTextPortion = rParaPortion.GetTextPortions()[nTmpPortion];
                if ( !rNextTextPortion.IsRightToLeft() && ( rNextTextPortion.GetKind() != PortionKind::TAB ) )
                    nX += rNextTextPortion.GetSize().Width();
                else
                    break;
                nTmpPortion++;
            }
            // Portions before must be added, visual before this portion
            nTmpPortion = nTextPortion;
            while ( nTmpPortion > rLine.GetStartPortion() )
            {
                --nTmpPortion;
                const TextPortion& rPrevTextPortion = rParaPortion.GetTextPortions()[nTmpPortion];
                if ( !rPrevTextPortion.IsRightToLeft() && ( rPrevTextPortion.GetKind() != PortionKind::TAB ) )
                    nX -= rPrevTextPortion.GetSize().Width();
                else
                    break;
            }
        }
    }
    if ( bR2LPara )
    {
        // Switch X positions...
        OSL_ENSURE( GetTextRanger() || GetPaperSize().Width(), "GetPortionXOffset - paper size?!" );
        OSL_ENSURE( GetTextRanger() || (nX <= GetPaperSize().Width()), "GetPortionXOffset - position out of paper size!" );
        nX = GetPaperSize().Width() - nX;
        nX -= rDestPortion.GetSize().Width();
    }

    return nX;
}

tools::Long ImpEditEngine::GetXPos(ParaPortion const& rParaPortion, EditLine const& rLine, sal_Int32 nIndex, bool bPreferPortionStart) const
{
    OSL_ENSURE( ( nIndex >= rLine.GetStart() ) && ( nIndex <= rLine.GetEnd() ) , "GetXPos has to be called properly!" );

    bool bDoPreferPortionStart = bPreferPortionStart;
    // Assure that the portion belongs to this line:
    if ( nIndex == rLine.GetStart() )
        bDoPreferPortionStart = true;
    else if ( nIndex == rLine.GetEnd() )
        bDoPreferPortionStart = false;

    sal_Int32 nTextPortionStart = 0;
    sal_Int32 nTextPortion = rParaPortion.GetTextPortions().FindPortion( nIndex, nTextPortionStart, bDoPreferPortionStart );

    OSL_ENSURE( ( nTextPortion >= rLine.GetStartPortion() ) && ( nTextPortion <= rLine.GetEndPortion() ), "GetXPos: Portion not in current line! " );

    const TextPortion& rPortion = rParaPortion.GetTextPortions()[nTextPortion];

    tools::Long nX = GetPortionXOffset(rParaPortion, rLine, nTextPortion);

    // calc text width, portion size may include CJK/CTL spacing...
    // But the array might not be init yet, if using text ranger this method is called within CreateLines()...
    tools::Long nPortionTextWidth = rPortion.GetSize().Width();
    if ( ( rPortion.GetKind() == PortionKind::TEXT ) && rPortion.GetLen() && !GetTextRanger() )
        nPortionTextWidth = rLine.GetCharPosArray()[nTextPortionStart + rPortion.GetLen() - 1 - rLine.GetStart()];

    if ( nTextPortionStart != nIndex )
    {
        // Search within portion...
        if ( nIndex == ( nTextPortionStart + rPortion.GetLen() ) )
        {
            // End of Portion
            if ( rPortion.GetKind() == PortionKind::TAB )
            {
                if ( nTextPortion+1 < rParaPortion.GetTextPortions().Count() )
                {
                    const TextPortion& rNextPortion = rParaPortion.GetTextPortions()[nTextPortion+1];
                    if ( rNextPortion.GetKind() != PortionKind::TAB )
                    {
                        if ( !bPreferPortionStart )
                            nX = GetXPos(rParaPortion, rLine, nIndex, true );
                        else if ( !IsRightToLeft( GetEditDoc().GetPos(rParaPortion.GetNode()) ) )
                            nX += nPortionTextWidth;
                    }
                }
                else if ( !IsRightToLeft( GetEditDoc().GetPos(rParaPortion.GetNode()) ) )
                {
                    nX += nPortionTextWidth;
                }
            }
            else if ( !rPortion.IsRightToLeft() )
            {
                nX += nPortionTextWidth;
            }
        }
        else if ( rPortion.GetKind() == PortionKind::TEXT )
        {
            OSL_ENSURE( nIndex != rLine.GetStart(), "Strange behavior in new GetXPos()" );
            OSL_ENSURE( !rLine.GetCharPosArray().empty(), "svx::ImpEditEngine::GetXPos(), portion in an empty line?" );

            if( !rLine.GetCharPosArray().empty() )
            {
                sal_Int32 nPos = nIndex - 1 - rLine.GetStart();
                if (nPos < 0 || o3tl::make_unsigned(nPos) >= rLine.GetCharPosArray().size())
                {
                    nPos = rLine.GetCharPosArray().size()-1;
                    OSL_FAIL("svx::ImpEditEngine::GetXPos(), index out of range!");
                }

                // old code restored see #i112788 (which leaves #i74188 unfixed again)
                tools::Long nPosInPortion = rLine.GetCharPosArray()[nPos];

                if ( !rPortion.IsRightToLeft() )
                {
                    nX += nPosInPortion;
                }
                else
                {
                    nX += nPortionTextWidth - nPosInPortion;
                }

                if ( rPortion.GetExtraInfos() && rPortion.GetExtraInfos()->bCompressed )
                {
                    nX += rPortion.GetExtraInfos()->nPortionOffsetX;
                    if ( rPortion.GetExtraInfos()->nAsianCompressionTypes & AsianCompressionFlags::PunctuationRight )
                    {
                        AsianCompressionFlags nType = GetCharTypeForCompression(rParaPortion.GetNode()->GetChar(nIndex));
                        if ( nType == AsianCompressionFlags::PunctuationRight && !rLine.GetCharPosArray().empty() )
                        {
                            sal_Int32 n = nIndex - nTextPortionStart;
                            const double* pDXArray = rLine.GetCharPosArray().data() + (nTextPortionStart - rLine.GetStart());
                            sal_Int32 nCharWidth = ( ( (n+1) < rPortion.GetLen() ) ? pDXArray[n] : rPortion.GetSize().Width() )
                                                            - ( n ? pDXArray[n-1] : 0 );
                            if ( (n+1) < rPortion.GetLen() )
                            {
                                // smaller, when char behind is AsianCompressionFlags::PunctuationRight also
                                nType = GetCharTypeForCompression(rParaPortion.GetNode()->GetChar(nIndex + 1));
                                if ( nType == AsianCompressionFlags::PunctuationRight )
                                {
                                    sal_Int32 nNextCharWidth = ( ( (n+2) < rPortion.GetLen() ) ? pDXArray[n+1] : rPortion.GetSize().Width() )
                                                                    - pDXArray[n];
                                    sal_Int32 nCompressed = nNextCharWidth/2;
                                    nCompressed *= rPortion.GetExtraInfos()->nMaxCompression100thPercent;
                                    nCompressed /= 10000;
                                    nCharWidth += nCompressed;
                                }
                            }
                            else
                            {
                                nCharWidth *= 2;    // last char pos to portion end is only compressed size
                            }
                            nX += nCharWidth/2; // 50% compression
                        }
                    }
                }
            }
        }
    }
    else // if ( nIndex == rLine.GetStart() )
    {
        if ( rPortion.IsRightToLeft() )
        {
            nX += nPortionTextWidth;
        }
    }

    return nX;
}

/** Is true if paragraph is in the empty cluster of paragraphs at the end */
bool ImpEditEngine::isInEmptyClusterAtTheEnd(const ParaPortion& rPortion, bool bIsScaling)
{
    sal_Int32 nPortion = GetParaPortions().GetPos(&rPortion);

    auto& rParagraphs = GetParaPortions();
    if (rParagraphs.Count() <= 0)
        return false;

    sal_Int32 nCurrent = rParagraphs.lastIndex();

    while (nCurrent > 0 && rParagraphs.getRef(nCurrent).IsEmpty())
    {
        if (nCurrent == nPortion)
        {
            OutlinerEditEng* pOutlEditEng{ dynamic_cast<OutlinerEditEng*>(mpEditEngine)};
            if (!bIsScaling && pOutlEditEng)
                return pOutlEditEng->GetDepth(nCurrent) < 0;
            else
                return true;
        }
        nCurrent--;
    }
    return false;
}

void ImpEditEngine::CalcHeight(ParaPortion& rPortion, bool bIsScaling)
{
    rPortion.mnHeight = 0;
    rPortion.mnFirstLineOffset = 0;

    if (!rPortion.IsVisible() || isInEmptyClusterAtTheEnd(rPortion, bIsScaling))
        return;

    OSL_ENSURE(rPortion.GetLines().Count(), "Paragraph with no lines in ParaPortion::CalcHeight");
    for (sal_Int32 nLine = 0; nLine < rPortion.GetLines().Count(); ++nLine)
        rPortion.mnHeight += rPortion.GetLines()[nLine].GetHeight();

    if (maStatus.IsOutliner())
        return;

    const SvxULSpaceItem& rULItem = rPortion.GetNode()->GetContentAttribs().GetItem( EE_PARA_ULSPACE );
    const SvxLineSpacingItem& rLSItem = rPortion.GetNode()->GetContentAttribs().GetItem( EE_PARA_SBL );
    sal_Int32 nSBL = ( rLSItem.GetInterLineSpaceRule() == SvxInterLineSpaceRule::Fix ) ? scaleYSpacingValue(rLSItem.GetInterLineSpace()) : 0;

    if ( nSBL )
    {
        if (rPortion.GetLines().Count() > 1)
            rPortion.mnHeight += (rPortion.GetLines().Count() - 1) * nSBL;
        if (maStatus.ULSpaceSummation())
            rPortion.mnHeight += nSBL;
    }

    sal_Int32 nPortion = GetParaPortions().GetPos(&rPortion);
    if ( nPortion )
    {
        sal_uInt16 nUpper = scaleYSpacingValue(rULItem.GetUpper());
        rPortion.mnHeight += nUpper;
        rPortion.mnFirstLineOffset = nUpper;
    }

    if (nPortion != GetParaPortions().lastIndex())
    {
        rPortion.mnHeight += scaleYSpacingValue(rULItem.GetLower());   // not in the last
    }

    if ( !nPortion || maStatus.ULSpaceSummation() )
        return;

    ParaPortion* pPrev = GetParaPortions().SafeGetObject( nPortion-1 );
    if (!pPrev)
        return;

    const SvxULSpaceItem& rPrevULItem = pPrev->GetNode()->GetContentAttribs().GetItem( EE_PARA_ULSPACE );
    const SvxLineSpacingItem& rPrevLSItem = pPrev->GetNode()->GetContentAttribs().GetItem( EE_PARA_SBL );

    // In relation between WinWord6/Writer3:
    // With a proportional line spacing the paragraph spacing is
    // also manipulated.
    // Only Writer3: Do not add up, but minimum distance.

    // check if distance by LineSpacing > Upper:
    sal_uInt16 nExtraSpace = scaleYSpacingValue(lcl_CalcExtraSpace(rLSItem));
    if (nExtraSpace > rPortion.mnFirstLineOffset)
    {
        // Paragraph becomes 'bigger':
        rPortion.mnHeight += (nExtraSpace - rPortion.mnFirstLineOffset);
        rPortion.mnFirstLineOffset = nExtraSpace;
    }

    // Determine nFirstLineOffset now f(pNode) => now f(pNode, pPrev):
    sal_uInt16 nPrevLower = scaleYSpacingValue(rPrevULItem.GetLower());

    // This PrevLower is still in the height of PrevPortion ...
    if (nPrevLower > rPortion.mnFirstLineOffset)
    {
        // Paragraph is 'small':
        rPortion.mnHeight -= rPortion.mnFirstLineOffset;
        rPortion.mnFirstLineOffset = 0;
    }
    else if ( nPrevLower )
    {
        // Paragraph becomes 'somewhat smaller':
        rPortion.mnHeight -= nPrevLower;
        rPortion.mnFirstLineOffset = rPortion.mnFirstLineOffset - nPrevLower;
    }
    // I find it not so good, but Writer3 feature:
    // Check if distance by LineSpacing > Lower: this value is not
    // stuck in the height of PrevPortion.
    if ( pPrev->IsInvalid() )
        return;

    nExtraSpace = scaleYSpacingValue(lcl_CalcExtraSpace(rPrevLSItem));
    if ( nExtraSpace > nPrevLower )
    {
        sal_uInt16 nMoreLower = nExtraSpace - nPrevLower;
        // Paragraph becomes 'bigger', 'grows' downwards:
        if ( nMoreLower > rPortion.mnFirstLineOffset )
        {
            rPortion.mnHeight += (nMoreLower - rPortion.mnFirstLineOffset);
            rPortion.mnFirstLineOffset = nMoreLower;
        }
    }
}

void ImpEditEngine::SetPaperSize(const Size& rNewSize)
{
    Size aOldSize = maPaperSize;
    SetValidPaperSize(rNewSize);
    Size aNewSize = maPaperSize;

    bool bAutoPageSize = GetStatus().AutoPageSize();
    if ( !(bAutoPageSize || ( aNewSize.Width() != aOldSize.Width() )) )
        return;

    for (EditView* pView : maEditViews)
    {
        if ( bAutoPageSize )
            pView->getImpl().RecalcOutputArea();
        else if (pView->getImpl().DoAutoSize())
        {
            pView->getImpl().ResetOutputArea(tools::Rectangle(pView->getImpl().GetOutputArea().TopLeft(), aNewSize));
        }
    }

    if ( bAutoPageSize || IsFormatted() )
    {
        // Changing the width has no effect for AutoPageSize, as this is
        // determined by the text width.
        // Optimization first after Vobis delivery was enabled ...
        FormatFullDoc();

        UpdateViews(mpActiveView);

        if (IsUpdateLayout() && mpActiveView)
            mpActiveView->ShowCursor(false, false);
    }
}

void ImpEditEngine::SetValidPaperSize( const Size& rNewSz )
{
    maPaperSize = rNewSz;

    tools::Long nMinWidth = maStatus.AutoPageWidth() ? maMinAutoPaperSize.Width() : 0;
    tools::Long nMaxWidth = maStatus.AutoPageWidth() ? maMaxAutoPaperSize.Width() : 0x7FFFFFFF;
    tools::Long nMinHeight = maStatus.AutoPageHeight() ? maMinAutoPaperSize.Height() : 0;
    tools::Long nMaxHeight = maStatus.AutoPageHeight() ? maMaxAutoPaperSize.Height() : 0x7FFFFFFF;

    // Minimum/Maximum width:
    if ( maPaperSize.Width() < nMinWidth )
        maPaperSize.setWidth( nMinWidth );
    else if ( maPaperSize.Width() > nMaxWidth )
        maPaperSize.setWidth( nMaxWidth );

    // Minimum/Maximum height:
    if ( maPaperSize.Height() < nMinHeight )
        maPaperSize.setHeight( nMinHeight );
    else if ( maPaperSize.Height() > nMaxHeight )
        maPaperSize.setHeight( nMaxHeight );
}

std::shared_ptr<SvxForbiddenCharactersTable> const & ImpEditEngine::GetForbiddenCharsTable()
{
    return EditDLL::Get().GetGlobalData()->GetForbiddenCharsTable();
}

void ImpEditEngine::SetForbiddenCharsTable(const std::shared_ptr<SvxForbiddenCharactersTable>& xForbiddenChars)
{
    EditDLL::Get().GetGlobalData()->SetForbiddenCharsTable( xForbiddenChars );
}

bool ImpEditEngine::IsVisualCursorTravelingEnabled()
{
    bool bVisualCursorTravaling = false;

    if ( SvtCTLOptions::IsCTLFontEnabled() && ( SvtCTLOptions::GetCTLCursorMovement() == SvtCTLOptions::MOVEMENT_VISUAL ) )
    {
        bVisualCursorTravaling = true;
    }

    return bVisualCursorTravaling;

}

bool ImpEditEngine::DoVisualCursorTraveling()
{
    // Don't check if it's necessary, because we also need it when leaving the paragraph
    return IsVisualCursorTravelingEnabled();
}

IMPL_LINK_NOARG(ImpEditEngine, DocModified, LinkParamNone*, void)
{
    maModifyHdl.Call( nullptr /*GetEditEnginePtr()*/ ); // NULL, because also used for Outliner
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
