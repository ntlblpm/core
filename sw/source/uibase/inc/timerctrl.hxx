/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <sfx2/stbitem.hxx>
#include <vcl/timer.hxx>
#include <chrono>

class SwTimerStatusBarControl final : public SfxStatusBarControl
{
private:
    AutoTimer m_aTimer;
    std::chrono::steady_clock::time_point m_aActiveStartTime;  // When window became active
    std::chrono::milliseconds m_nAccumulatedTime;  // Total active time in milliseconds
    bool m_bIsActive;  // Track if window is currently active

    DECL_LINK(TimerHdl, Timer*, void);

public:
    SFX_DECL_STATUSBAR_CONTROL();

    SwTimerStatusBarControl(sal_uInt16 nSlot, sal_uInt16 nCtrlId, StatusBar& rStb);
    virtual ~SwTimerStatusBarControl() override;

    virtual void StateChangedAtStatusBarControl(sal_uInt16 nSID, SfxItemState eState,
                                               const SfxPoolItem* pState) override;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */