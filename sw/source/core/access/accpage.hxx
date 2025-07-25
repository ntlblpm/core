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

#ifndef INCLUDED_SW_SOURCE_CORE_ACCESS_ACCPAGE_HXX
#define INCLUDED_SW_SOURCE_CORE_ACCESS_ACCPAGE_HXX

#include "acccontext.hxx"

/**
 * accessibility implementation for the page (SwPageFrame)
 * The page is _only_ visible in the page preview. For the regular
 * document view, it doesn't make sense to add this additional element
 * into the hierarchy. For the page preview, however, the page is the
 * important.
 */
class SwAccessiblePage : public SwAccessibleContext
{
    bool    m_bIsSelected;    // protected by base class mutex

    bool    IsSelected();

    using SwAccessibleFrame::GetBounds;

protected:
    // Set states for getAccessibleStateSet.
    // This derived class additionally sets
    // FOCUSABLE(1) and FOCUSED(+)
    virtual void GetStates( sal_Int64& rStateSet ) override;

    virtual void InvalidateCursorPos_() override;
    virtual void InvalidateFocus_() override;

    virtual ~SwAccessiblePage() override;

public:
    // convenience constructor to avoid typecast;
    // may only be called with SwPageFrame argument
    SwAccessiblePage(std::shared_ptr<SwAccessibleMap> const& pInitMap,
                     const SwFrame* pFrame);

    // XAccessibleContext methods that need to be overridden

    virtual OUString SAL_CALL getAccessibleDescription() override;

    virtual bool HasCursor() override;   // required by map to remember that object
};

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
