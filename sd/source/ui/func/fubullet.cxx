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

#include <fubullet.hxx>

#include <sfx2/bindings.hxx>
#include <sfx2/viewfrm.hxx>
#include <editeng/eeitem.hxx>
#include <editeng/editund2.hxx>
#include <svl/poolitem.hxx>
#include <editeng/fontitem.hxx>
#include <OutlineView.hxx>
#include <OutlineViewShell.hxx>
#include <DrawViewShell.hxx>
#include <ViewShellBase.hxx>
#include <Window.hxx>
#include <drawdoc.hxx>
#include <strings.hrc>
#include <sdresid.hxx>
#include <svx/svdoutl.hxx>
#include <sfx2/request.hxx>
#include <svl/ctloptions.hxx>
#include <svl/stritem.hxx>
#include <tools/debug.hxx>
#include <Outliner.hxx>
#include <svx/svxdlg.hxx>
#include <svx/svxids.hrc>

namespace sd {

const sal_Unicode CHAR_HARDBLANK    =   u'\x00A0';
const sal_Unicode CHAR_HARDHYPHEN   =   u'\x2011';
const sal_Unicode CHAR_SOFTHYPHEN   =   u'\x00AD';
const sal_Unicode CHAR_RLM          =   u'\x200F';
const sal_Unicode CHAR_LRM          =   u'\x200E';
const sal_Unicode CHAR_ZWSP         =   u'\x200B';
const sal_Unicode CHAR_WJ           =   u'\x2060';
const sal_Unicode CHAR_NNBSP        =   u'\x202F'; //NARROW NO-BREAK SPACE


FuBullet::FuBullet (
    ViewShell& rViewSh,
    ::sd::Window* pWin,
    ::sd::View* _pView,
    SdDrawDocument& rDoc,
    SfxRequest& rReq)
    : FuPoor(rViewSh, pWin, _pView, rDoc, rReq)
{
}

rtl::Reference<FuPoor> FuBullet::Create( ViewShell& rViewSh, ::sd::Window* pWin, ::sd::View* pView, SdDrawDocument& rDoc, SfxRequest& rReq )
{
    rtl::Reference<FuPoor> xFunc( new FuBullet( rViewSh, pWin, pView, rDoc, rReq ) );
    xFunc->DoExecute(rReq);
    return xFunc;
}

void FuBullet::DoExecute( SfxRequest& rReq )
{
    if( rReq.GetSlot() == SID_CHARMAP )
        InsertSpecialCharacter(rReq);
    else
    {
        sal_Unicode cMark = 0;
        switch( rReq.GetSlot() )
        {
            case FN_INSERT_SOFT_HYPHEN: cMark = CHAR_SOFTHYPHEN ; break;
            case FN_INSERT_HARDHYPHEN:  cMark = CHAR_HARDHYPHEN ; break;
            case FN_INSERT_HARD_SPACE:  cMark = CHAR_HARDBLANK ; break;
            case FN_INSERT_NNBSP:  cMark = CHAR_NNBSP ; break;
            case SID_INSERT_RLM : cMark = CHAR_RLM ; break;
            case SID_INSERT_LRM : cMark = CHAR_LRM ; break;
            case SID_INSERT_ZWSP : cMark = CHAR_ZWSP ; break;
            case SID_INSERT_WJ: cMark = CHAR_WJ; break;
        }

        DBG_ASSERT( cMark != 0, "FuBullet::FuBullet(), illegal slot used!" );

        if( cMark )
            InsertFormattingMark( cMark );
    }

}

void FuBullet::InsertFormattingMark( sal_Unicode cMark )
{
    OutlinerView* pOV = nullptr;
    ::Outliner*   pOL = nullptr;

    // depending on ViewShell set Outliner and OutlinerView
    if( dynamic_cast< const DrawViewShell *>( &mrViewShell ) !=  nullptr)
    {
        pOV = mpView->GetTextEditOutlinerView();
        if (pOV)
            pOL = mpView->GetTextEditOutliner();
    }
    else if( dynamic_cast< const OutlineViewShell *>( &mrViewShell ) !=  nullptr)
    {
        pOL = &static_cast<OutlineView*>(mpView)->GetOutliner();
        pOV = static_cast<OutlineView*>(mpView)->GetViewByWindow(
            mrViewShell.GetActiveWindow());
    }

    // insert string
    if(!(pOV && pOL))
        return;

    // prevent flickering
    pOV->HideCursor();
    pOL->SetUpdateLayout(false);

    // remove old selected text
    pOV->InsertText( u""_ustr );

    // prepare undo
    EditUndoManager& rUndoMgr =  pOL->GetUndoManager();
    rUndoMgr.EnterListAction(SdResId(STR_UNDO_INSERT_SPECCHAR),
                                u""_ustr, 0, mrViewShell.GetViewShellBase().GetViewShellId() );

    // insert given text
    OUString aStr( cMark );
    pOV->InsertText( aStr, true);

    ESelection aSel = pOV->GetSelection();
    aSel.CollapseToEnd();
    pOV->SetSelection(aSel);

    rUndoMgr.LeaveListAction();

    // restart repainting
    pOL->SetUpdateLayout(true);
    pOV->ShowCursor();
}

void FuBullet::InsertSpecialCharacter( SfxRequest const & rReq )
{
    const SfxItemSet *pArgs = rReq.GetArgs();
    const SfxStringItem* pItem = nullptr;
    if( pArgs )
        pItem = pArgs->GetItemIfSet(SID_CHARMAP, false);

    OUString aChars;
    vcl::Font aFont;
    if ( pItem )
    {
        aChars = pItem->GetValue();
        const SfxStringItem* pFontItem = pArgs->GetItemIfSet( SID_ATTR_SPECIALCHAR, false );
        if ( pFontItem )
        {
            const OUString& aFontName = pFontItem->GetValue();
            aFont = vcl::Font( aFontName, Size(1,1) );
        }
        else
        {
            SfxItemSet aFontAttr( mrDoc.GetPool() );
            mpView->GetAttributes( aFontAttr );
            const SvxFontItem* pFItem = aFontAttr.GetItem( SID_ATTR_CHAR_FONT );
            if( pFItem )
                aFont = vcl::Font( pFItem->GetFamilyName(), pFItem->GetStyleName(), Size( 1, 1 ) );
        }
    }

    if (aChars.isEmpty())
    {
        SfxAllItemSet aSet( mrDoc.GetPool() );
        aSet.Put( SfxBoolItem( FN_PARAM_1, false ) );

        SfxItemSet aFontAttr( mrDoc.GetPool() );
        mpView->GetAttributes( aFontAttr );
        const SvxFontItem* pFontItem = aFontAttr.GetItem( SID_ATTR_CHAR_FONT );
        if( pFontItem )
            aSet.Put( *pFontItem );

        SvxAbstractDialogFactory* pFact = SvxAbstractDialogFactory::Create();
        css::uno::Reference<css::frame::XFrame> xFrame;
        if (SfxViewFrame* pFrame = mrViewShell.GetFrame())
            xFrame = pFrame->GetFrame().GetFrameInterface();
        VclPtr<SfxAbstractDialog> pDlg( pFact->CreateCharMapDialog(mpView->GetViewShell()->GetFrameWeld(), aSet,
            xFrame) );

        // If a character is selected, it can be shown
        // pDLg->SetFont( );
        // pDlg->SetChar( );
        pDlg->StartExecuteAsync(
            [pDlg] (sal_Int32 /*nResult*/)->void
            {
                pDlg->disposeOnce();
            }
        );
        return;
    }

    if (aChars.isEmpty())
        return;

    OutlinerView* pOV = nullptr;
    ::Outliner*   pOL = nullptr;

    // determine depending on ViewShell Outliner and OutlinerView
    if(dynamic_cast< const DrawViewShell *>( &mrViewShell ))
    {
        pOV = mpView->GetTextEditOutlinerView();
        if (pOV)
        {
            pOL = mpView->GetTextEditOutliner();
        }
    }
    else if(dynamic_cast< const OutlineViewShell *>( &mrViewShell ))
    {
        pOL = &static_cast<OutlineView*>(mpView)->GetOutliner();
        pOV = static_cast<OutlineView*>(mpView)->GetViewByWindow(
            mrViewShell.GetActiveWindow());
    }

    // insert special character
    if (!pOV)
        return;

    // prevent flicker
    pOV->HideCursor();
    pOL->SetUpdateLayout(false);

    /* remember old attributes:
       To do that, remove selected area before (it has to go anyway).
       With that, we get unique attributes (and since there is no
       DeleteSelected() in OutlinerView, it is deleted by inserting an
       empty string). */
    pOV->InsertText( u""_ustr );

    SfxItemSetFixed<EE_CHAR_FONTINFO, EE_CHAR_FONTINFO> aOldSet( mrDoc.GetPool() );
    aOldSet.Put( pOV->GetAttribs() );

    EditUndoManager& rUndoMgr = pOL->GetUndoManager();
    ViewShellId nViewShellId = mrViewShell.GetViewShellBase().GetViewShellId();
    rUndoMgr.EnterListAction(SdResId(STR_UNDO_INSERT_SPECCHAR),
                             u""_ustr, 0, nViewShellId );
    pOV->InsertText(aChars, true);

    // set attributes (set font)
    SfxItemSet aSet(pOL->GetEmptyItemSet());
    SvxFontItem aFontItem (aFont.GetFamilyTypeMaybeAskConfig(), aFont.GetFamilyName(),
                           aFont.GetStyleName(), aFont.GetPitchMaybeAskConfig(),
                           aFont.GetCharSet(),
                           EE_CHAR_FONTINFO);
    aSet.Put(aFontItem);
    aFontItem.SetWhich(EE_CHAR_FONTINFO_CJK);
    aSet.Put(aFontItem);
    aFontItem.SetWhich(EE_CHAR_FONTINFO_CTL);
    aSet.Put(aFontItem);
    pOV->SetAttribs(aSet);

    ESelection aSel = pOV->GetSelection();
    aSel.CollapseToEnd();
    pOV->SetSelection(aSel);

    // do not go ahead with setting attributes of special characters
    pOV->GetOutliner().QuickSetAttribs(aOldSet, aSel);

    rUndoMgr.LeaveListAction();

    // show it again
    pOL->SetUpdateLayout(true);
    pOV->ShowCursor();
}

void FuBullet::GetSlotState( SfxItemSet& rSet, ViewShell const * pViewShell, SfxViewFrame* pViewFrame )
{
    if( !(SfxItemState::DEFAULT == rSet.GetItemState( SID_CHARMAP ) ||
        SfxItemState::DEFAULT == rSet.GetItemState( SID_CHARMAP_CONTROL ) ||
        SfxItemState::DEFAULT == rSet.GetItemState( FN_INSERT_SOFT_HYPHEN ) ||
        SfxItemState::DEFAULT == rSet.GetItemState( FN_INSERT_HARDHYPHEN ) ||
        SfxItemState::DEFAULT == rSet.GetItemState( FN_INSERT_HARD_SPACE ) ||
        SfxItemState::DEFAULT == rSet.GetItemState( FN_INSERT_NNBSP ) ||
        SfxItemState::DEFAULT == rSet.GetItemState( SID_INSERT_RLM ) ||
        SfxItemState::DEFAULT == rSet.GetItemState( SID_INSERT_LRM ) ||
        SfxItemState::DEFAULT == rSet.GetItemState( SID_INSERT_WJ ) ||
        SfxItemState::DEFAULT == rSet.GetItemState( SID_INSERT_ZWSP )))
        return;

    ::sd::View* pView = pViewShell ? pViewShell->GetView() : nullptr;
    OutlinerView* pOLV = pView ? pView->GetTextEditOutlinerView() : nullptr;

    const bool bTextEdit = pOLV;

    const bool bCtlEnabled = SvtCTLOptions::IsCTLFontEnabled();

    if(!bTextEdit )
    {
        rSet.DisableItem(FN_INSERT_SOFT_HYPHEN);
        rSet.DisableItem(FN_INSERT_HARDHYPHEN);
        rSet.DisableItem(FN_INSERT_HARD_SPACE);
        rSet.DisableItem(FN_INSERT_NNBSP);
        rSet.DisableItem(SID_INSERT_WJ);
        rSet.DisableItem(SID_INSERT_ZWSP);
    }

    if( !bTextEdit && (dynamic_cast<OutlineViewShell const *>( pViewShell ) == nullptr) )
    {
        rSet.DisableItem(SID_CHARMAP);
        rSet.DisableItem(SID_CHARMAP_CONTROL);
    }

    if(!bTextEdit || !bCtlEnabled )
    {
        rSet.DisableItem(SID_INSERT_RLM);
        rSet.DisableItem(SID_INSERT_LRM);
    }

    if( pViewFrame )
    {
        SfxBindings& rBindings = pViewFrame->GetBindings();

        rBindings.SetVisibleState( SID_INSERT_RLM, bCtlEnabled );
        rBindings.SetVisibleState( SID_INSERT_LRM, bCtlEnabled );
    }
}
} // end of namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
