/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <timerctrl.hxx>
#include <svl/stritem.hxx>
#include <vcl/status.hxx>
#include <vcl/window.hxx>
#include <rtl/ustrbuf.hxx>

SFX_IMPL_STATUSBAR_CONTROL(SwTimerStatusBarControl, SfxStringItem);

SwTimerStatusBarControl::SwTimerStatusBarControl(
    sal_uInt16 nSlot, sal_uInt16 nCtrlId, StatusBar& rStb)
    : SfxStatusBarControl(nSlot, nCtrlId, rStb)
    , m_aTimer("SwTimerStatusBarControl Timer")
    , m_nAccumulatedTime(0)
    , m_bIsActive(false)
{
    // Check initial window state
    vcl::Window* pWindow = &GetStatusBar();
    vcl::Window* pFrameWindow = pWindow->GetFrameWindow();
    if (pFrameWindow && pFrameWindow->IsActive())
    {
        m_bIsActive = true;
        m_aActiveStartTime = std::chrono::steady_clock::now();
    }

    m_aTimer.SetInvokeHandler(LINK(this, SwTimerStatusBarControl, TimerHdl));
    m_aTimer.SetTimeout(1000); // Update every second
    m_aTimer.Start();

    // Set initial text
    GetStatusBar().SetItemText(GetId(), "Timer: 00:00:00");
}

SwTimerStatusBarControl::~SwTimerStatusBarControl()
{
    m_aTimer.Stop();

    // If window is still active, accumulate the final time
    if (m_bIsActive)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_aActiveStartTime);
        m_nAccumulatedTime += elapsed;
    }
}

IMPL_LINK_NOARG(SwTimerStatusBarControl, TimerHdl, Timer*, void)
{
    auto now = std::chrono::steady_clock::now();

    // Check if the window has focus
    vcl::Window* pWindow = &GetStatusBar();
    vcl::Window* pFrameWindow = pWindow->GetFrameWindow();
    bool bIsActiveNow = pFrameWindow && pFrameWindow->IsActive();

    // Handle state transitions
    if (bIsActiveNow && !m_bIsActive)
    {
        // Window just became active
        m_aActiveStartTime = now;
        m_bIsActive = true;
    }
    else if (!bIsActiveNow && m_bIsActive)
    {
        // Window just became inactive - accumulate the active time
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_aActiveStartTime);
        m_nAccumulatedTime += elapsed;
        m_bIsActive = false;
    }

    // Calculate total time including current active session
    auto totalTime = m_nAccumulatedTime;
    if (m_bIsActive)
    {
        auto currentSession = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_aActiveStartTime);
        totalTime += currentSession;
    }

    // Display the accumulated time
    int totalSeconds = totalTime.count() / 1000;
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;

    OUStringBuffer aText("Timer: ");
    if (hours < 10) aText.append("0");
    aText.append(OUString::number(hours));
    aText.append(":");
    if (minutes < 10) aText.append("0");
    aText.append(OUString::number(minutes));
    aText.append(":");
    if (seconds < 10) aText.append("0");
    aText.append(OUString::number(seconds));

    GetStatusBar().SetItemText(GetId(), aText.makeStringAndClear());
}

void SwTimerStatusBarControl::StateChangedAtStatusBarControl(
    sal_uInt16, SfxItemState eState, const SfxPoolItem* pState)
{
    // We manage our own text via the timer, so we ignore state changes
    // But we could use this to start/stop the timer based on document state
    if (eState == SfxItemState::DEFAULT && pState)
    {
        // Timer keeps running regardless of state
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */