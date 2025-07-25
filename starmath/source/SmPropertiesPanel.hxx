/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
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

#pragma once

#include <sal/config.h>

#include <sfx2/sidebar/PanelLayout.hxx>

#include <map>
#include <memory>

namespace com::sun::star::frame
{
class XFrame;
}

namespace sm::sidebar
{
class SmPropertiesPanel : public PanelLayout
{
public:
    static std::unique_ptr<PanelLayout>
    Create(weld::Widget& rParent, const css::uno::Reference<css::frame::XFrame>& xFrame);
    SmPropertiesPanel(weld::Widget& rParent, const css::uno::Reference<css::frame::XFrame>& xFrame);
    ~SmPropertiesPanel();

private:
    DECL_LINK(ButtonClickHandler, weld::Button&, void);

    css::uno::Reference<css::frame::XFrame> mxFrame;

    std::unique_ptr<weld::Button> mpFormatFontsButton;
    std::unique_ptr<weld::Button> mpFormatFontSizeButton;
    std::unique_ptr<weld::Button> mpFormatSpacingButton;
    std::unique_ptr<weld::Button> mpFormatAlignmentButton;

    std::map<weld::Button*, OUString> maButtonCommands;
};

} // end of namespace sm::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
