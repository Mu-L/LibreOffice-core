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

#include <fusearch.hxx>

#include <sfx2/viewfrm.hxx>

#include <sfx2/bindings.hxx>
#include <fupoor.hxx>
#include <drawdoc.hxx>
#include <app.hrc>
#include <Outliner.hxx>
#include <DrawDocShell.hxx>
#include <DrawViewShell.hxx>
#include <OutlineViewShell.hxx>
#include <NotesPanelViewShell.hxx>
#include <ViewShellBase.hxx>

class SfxRequest;

namespace sd {

const sal_uInt16 SidArraySpell[] = {
            SID_DRAWINGMODE,
            SID_OUTLINE_MODE,
            SID_SLIDE_SORTER_MODE,
            SID_NOTES_MODE,
            SID_HANDOUT_MASTER_MODE,
            SID_SLIDE_MASTER_MODE,
            SID_NOTES_MASTER_MODE,
            0 };

FuSearch::FuSearch (
    ViewShell& rViewSh,
    ::sd::Window* pWin,
    ::sd::View* pView,
    SdDrawDocument& rDoc,
    SfxRequest& rReq )
    : FuPoor(rViewSh, pWin, pView, rDoc, rReq),
      m_pSdOutliner(nullptr),
      m_bOwnOutliner(false)
{
}

FuSearch* FuSearch::createPtr(ViewShell& rViewSh, ::sd::Window* pWin, ::sd::View* pView, SdDrawDocument& rDoc, SfxRequest& rReq)
{
    FuSearch* xFunc( new FuSearch( rViewSh, pWin, pView, rDoc, rReq ) );
    xFunc->DoExecute(rReq);
    return xFunc;
}

void FuSearch::DoExecute( SfxRequest& )
{
    mrViewShell.GetViewFrame()->GetBindings().Invalidate( SidArraySpell );

    if ( dynamic_cast< const DrawViewShell *>( &mrViewShell ) !=  nullptr )
    {
        m_bOwnOutliner = true;
        m_pSdOutliner = new SdOutliner( mrDoc, OutlinerMode::TextObject );
    }
    else if ( dynamic_cast< const OutlineViewShell *>( &mrViewShell ) !=  nullptr )
    {
        m_bOwnOutliner = false;
        m_pSdOutliner = mrDoc.GetOutliner();
    }
    else if ( dynamic_cast< const NotesPanelViewShell *>( &mrViewShell ) !=  nullptr )
    {
        ViewShell::ShellType nShellType = mrViewShell.GetViewShellBase().GetMainViewShell()->GetShellType();
        if( nShellType == ViewShell::ST_OUTLINE )
        {
            m_bOwnOutliner = false;
            m_pSdOutliner = mrDoc.GetOutliner();
        }
        if( nShellType == ViewShell::ST_IMPRESS )
        {
            m_bOwnOutliner = true;
            m_pSdOutliner = new SdOutliner( mrDoc, OutlinerMode::TextObject );
        }
    }

    if (m_pSdOutliner)
       m_pSdOutliner->PrepareSpelling();
}

FuSearch::~FuSearch()
{
    if ( ! mpDocSh->IsInDestruction() && mpDocSh->GetViewShell()!=nullptr)
        mpDocSh->GetViewShell()->GetViewFrame()->GetBindings().Invalidate( SidArraySpell );

    if (m_pSdOutliner)
        m_pSdOutliner->EndSpelling();

    if (m_bOwnOutliner)
        delete m_pSdOutliner;
}

void FuSearch::SearchAndReplace( const SvxSearchItem* pSearchItem )
{
    ViewShellBase* pBase = dynamic_cast<ViewShellBase*>( SfxViewShell::Current() );
    ViewShell* pViewShell = nullptr;
    if (pBase != nullptr)
        pViewShell = pBase->GetMainViewShell().get();

    if (pViewShell == nullptr)
        return;

    if (m_pSdOutliner && dynamic_cast<const DrawViewShell*>(pViewShell) && !m_bOwnOutliner)
    {
        m_pSdOutliner->EndSpelling();

        m_bOwnOutliner = true;
        m_pSdOutliner = new SdOutliner(mrDoc, OutlinerMode::TextObject);
        m_pSdOutliner->PrepareSpelling();
    }
    else if (m_pSdOutliner && dynamic_cast<const OutlineViewShell*>(pViewShell) && m_bOwnOutliner)
    {
        m_pSdOutliner->EndSpelling();
        delete m_pSdOutliner;

        m_bOwnOutliner = false;
        m_pSdOutliner = mrDoc.GetOutliner();
        m_pSdOutliner->PrepareSpelling();
    }

    if (m_pSdOutliner)
    {
        bool bEndSpelling = m_pSdOutliner->StartSearchAndReplace(pSearchItem);

        if (bEndSpelling)
        {
            m_pSdOutliner->EndSpelling();
            m_pSdOutliner->PrepareSpelling();
        }
    }
}

} // end of namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
