/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <readingtimectrl.hxx>
#include <svl/stritem.hxx>
#include <vcl/status.hxx>
#include <rtl/ustrbuf.hxx>

SFX_IMPL_STATUSBAR_CONTROL(SwReadingTimeStatusBarControl, SfxStringItem);

SwReadingTimeStatusBarControl::SwReadingTimeStatusBarControl(
    sal_uInt16 nSlot, sal_uInt16 nCtrlId, StatusBar& rStb)
    : SfxStatusBarControl(nSlot, nCtrlId, rStb)
{
    // Initial text will be set by StateChangedAtStatusBarControl
}

SwReadingTimeStatusBarControl::~SwReadingTimeStatusBarControl()
{
}

void SwReadingTimeStatusBarControl::StateChangedAtStatusBarControl(
    sal_uInt16, SfxItemState eState, const SfxPoolItem* pState)
{
    if (eState == SfxItemState::DEFAULT && pState)
    {
        const SfxStringItem* pItem = static_cast<const SfxStringItem*>(pState);
        GetStatusBar().SetItemText(GetId(), pItem->GetValue());
    }
    else
    {
        // No state available - show empty or placeholder text
        GetStatusBar().SetItemText(GetId(), "--");
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */