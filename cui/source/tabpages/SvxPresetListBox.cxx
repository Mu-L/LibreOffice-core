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

#include <SvxPresetListBox.hxx>

#include <svx/xtable.hxx>
#include <vcl/commandevent.hxx>
#include <vcl/event.hxx>
#include <vcl/image.hxx>
#include <vcl/svapp.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/Menu.hxx>
#include <vcl/weld/ScrolledWindow.hxx>

SvxPresetListBox::SvxPresetListBox(std::unique_ptr<weld::ScrolledWindow> pWindow)
    : ValueSet(std::move(pWindow))
    , m_aIconSize(60, 64)
{
    SetEdgeBlending(true);
}

void SvxPresetListBox::SetDrawingArea(weld::DrawingArea* pDrawingArea)
{
    ValueSet::SetDrawingArea(pDrawingArea);
    SetStyle(GetStyle() | WB_ITEMBORDER | WB_VSCROLL);
    SetColCount(3);
    SetLineCount(5);
}

bool SvxPresetListBox::Command(const CommandEvent& rEvent)
{
    if (rEvent.GetCommand() != CommandEventId::ContextMenu)
        return CustomWidgetController::Command(rEvent);

    sal_uInt16 nContextMenuItemId = 0;
    Point aPos;
    if (rEvent.IsMouseEvent())
    {
        nContextMenuItemId = GetHighlightedItemId();
        aPos = rEvent.GetMousePosPixel();
    }
    else
    {
        nContextMenuItemId = GetSelectedItemId();
        aPos = GetItemRect(nContextMenuItemId).Center();
    }

    if (nContextMenuItemId > 0)
    {
        std::unique_ptr<weld::Builder> xBuilder(
            Application::CreateBuilder(GetDrawingArea(), u"svx/ui/presetmenu.ui"_ustr));
        std::unique_ptr<weld::Menu> xMenu(xBuilder->weld_menu(u"menu"_ustr));
        const OUString sIdent
            = xMenu->popup_at_rect(GetDrawingArea(), tools::Rectangle(aPos, Size(1, 1)));
        if (sIdent == u"rename")
            maRenameHdl.Call(nContextMenuItemId);
        else if (sIdent == u"delete")
            maDeleteHdl.Call(nContextMenuItemId);

        return true;
    }
    return false;
}

bool SvxPresetListBox::SvxPresetListBox::KeyInput(const KeyEvent& rKEvt)
{
    switch (rKEvt.GetKeyCode().GetCode())
    {
        case KEY_DELETE:
        {
            maDeleteHdl.Call(GetSelectedItemId());
            return true;
        }
        break;
        default:
            return ValueSet::KeyInput(rKEvt);
    }
}

template <typename ListType, typename EntryType>
void SvxPresetListBox::FillPresetListBoxImpl(ListType& rList)
{
    const Size aSize(GetIconSize());
    sal_uInt32 nStartIndex = 1;
    for (tools::Long nIndex = 0; nIndex < rList.Count(); nIndex++, nStartIndex++)
    {
        Bitmap aBitmap = rList.GetBitmapForPreview(nIndex, aSize);
        EntryType* pItem = static_cast<EntryType*>(rList.Get(nIndex));
        InsertItem(nStartIndex, Image(aBitmap), pItem->GetName());
    }
}

void SvxPresetListBox::FillPresetListBox(XGradientList& rList)
{
    FillPresetListBoxImpl<XGradientList, XGradientEntry>(rList);
}

void SvxPresetListBox::FillPresetListBox(XHatchList& rList)
{
    FillPresetListBoxImpl<XHatchList, XHatchEntry>(rList);
}

void SvxPresetListBox::FillPresetListBox(XBitmapList& rList)
{
    FillPresetListBoxImpl<XBitmapList, XBitmapEntry>(rList);
}

void SvxPresetListBox::FillPresetListBox(XPatternList& rList)
{
    FillPresetListBoxImpl<XPatternList, XBitmapEntry>(rList);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
