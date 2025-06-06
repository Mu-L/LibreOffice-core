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

#include <fuoaprms.hxx>
#include <sdattr.hrc>

#include <editeng/colritem.hxx>
#include <svx/svdundo.hxx>
#include <sfx2/objsh.hxx>
#include <sfx2/request.hxx>
#include <sfx2/viewfrm.hxx>
#include <sfx2/sfxdlg.hxx>
#include <svl/intitem.hxx>
#include <svl/stritem.hxx>
#include <svx/svdopath.hxx>
#include <tools/debug.hxx>

#include <strings.hrc>
#include <drawdoc.hxx>
#include <ViewShell.hxx>
#include <ViewShellBase.hxx>
#include <anminfo.hxx>
#include <unoaprms.hxx>
#include <sdundogr.hxx>
#include <View.hxx>
#include <sdabstdlg.hxx>
#include <sdresid.hxx>
#include <tools/helpers.hxx>
#include <tpaction.hxx>
#include <basegfx/polygon/b2dpolygon.hxx>
#include <memory>

using namespace ::com::sun::star;

namespace sd {


#define ATTR_MISSING    0       ///< Attribute missing
#define ATTR_MIXED      1       ///< Attribute ambiguous (on multi-selection)
#define ATTR_SET        2       ///< Attribute unique

FuObjectAnimationParameters::FuObjectAnimationParameters (
    ViewShell&   rViewSh,
    ::sd::Window*        pWin,
    ::sd::View*      pView,
    SdDrawDocument& rDoc,
    SfxRequest&  rReq)
    : FuPoor(rViewSh, pWin, pView, rDoc, rReq)
{
}

rtl::Reference<FuPoor> FuObjectAnimationParameters::Create( ViewShell& rViewSh, ::sd::Window* pWin, ::sd::View* pView, SdDrawDocument& rDoc, SfxRequest& rReq )
{
    rtl::Reference<FuPoor> xFunc( new FuObjectAnimationParameters( rViewSh, pWin, pView, rDoc, rReq ) );
    xFunc->DoExecute(rReq);
    return xFunc;
}

void FuObjectAnimationParameters::DoExecute( SfxRequest& rReq )
{
    const SdrMarkList& rMarkList  = mpView->GetMarkedObjectList();
    const size_t nCount = rMarkList.GetMarkCount();

    short nAnimationSet     = ATTR_MISSING;
    short nEffectSet        = ATTR_MISSING;
    short nTextEffectSet    = ATTR_MISSING;
    short nSpeedSet         = ATTR_MISSING;
    short nFadeColorSet     = ATTR_MISSING;
    short nFadeOutSet       = ATTR_MISSING;
    short nInvisibleSet     = ATTR_MISSING;
    short nSoundOnSet       = ATTR_MISSING;
    short nSoundFileSet     = ATTR_MISSING;
    short nPlayFullSet      = ATTR_MISSING;
    short nClickActionSet   = ATTR_MISSING;
    short nBookmarkSet      = ATTR_MISSING;

    short nSecondEffectSet      = ATTR_MISSING;
    short nSecondSpeedSet       = ATTR_MISSING;
    short nSecondSoundOnSet     = ATTR_MISSING;
    short nSecondPlayFullSet    = ATTR_MISSING;

    // defaults (for Undo-Action)
    presentation::AnimationEffect eEffect         = presentation::AnimationEffect_NONE;
    presentation::AnimationEffect eTextEffect     = presentation::AnimationEffect_NONE;
    presentation::AnimationSpeed  eSpeed          = presentation::AnimationSpeed_MEDIUM;
    bool            bActive         = false;
    bool            bFadeOut        = false;
    Color           aFadeColor      = COL_LIGHTGRAY;
    bool            bInvisible      = false;
    bool            bSoundOn        = false;
    OUString        aSound;
    bool            bPlayFull       = false;
    presentation::ClickAction     eClickAction    = presentation::ClickAction_NONE;
    OUString        aBookmark;

    presentation::AnimationEffect eSecondEffect   = presentation::AnimationEffect_NONE;
    presentation::AnimationSpeed  eSecondSpeed    = presentation::AnimationSpeed_MEDIUM;
    bool            bSecondSoundOn  = false;
    bool            bSecondPlayFull = false;

    SdAnimationInfo* pInfo;
    SdrMark* pMark;

    // inspect first object
    pMark = rMarkList.GetMark(0);
    pInfo = SdDrawDocument::GetAnimationInfo(pMark->GetMarkedSdrObj());
    if( pInfo )
    {
        bActive             = pInfo->mbActive;
        nAnimationSet       = ATTR_SET;

        eEffect             = pInfo->meEffect;
        nEffectSet          = ATTR_SET;

        eTextEffect         = pInfo->meTextEffect;
        nTextEffectSet      = ATTR_SET;

        eSpeed              = pInfo->meSpeed;
        nSpeedSet           = ATTR_SET;

        bFadeOut            = pInfo->mbDimPrevious;
        nFadeOutSet         = ATTR_SET;

        aFadeColor          = pInfo->maDimColor;
        nFadeColorSet       = ATTR_SET;

        bInvisible          = pInfo->mbDimHide;
        nInvisibleSet       = ATTR_SET;

        bSoundOn            = pInfo->mbSoundOn;
        nSoundOnSet         = ATTR_SET;

        aSound              = pInfo->maSoundFile;
        nSoundFileSet       = ATTR_SET;

        bPlayFull           = pInfo->mbPlayFull;
        nPlayFullSet        = ATTR_SET;

        eClickAction        = pInfo->meClickAction;
        nClickActionSet     = ATTR_SET;

        aBookmark           = pInfo->GetBookmark();
        nBookmarkSet        = ATTR_SET;

        eSecondEffect       = pInfo->meSecondEffect;
        nSecondEffectSet    = ATTR_SET;

        eSecondSpeed        = pInfo->meSecondSpeed;
        nSecondSpeedSet     = ATTR_SET;

        bSecondSoundOn      = pInfo->mbSecondSoundOn;
        nSecondSoundOnSet   = ATTR_SET;

        bSecondPlayFull     = pInfo->mbSecondPlayFull;
        nSecondPlayFullSet  = ATTR_SET;
    }

    // if necessary, inspect more objects
    for( size_t nObject = 1; nObject < nCount; ++nObject )
    {
        pMark = rMarkList.GetMark( nObject );
        SdrObject* pObject = pMark->GetMarkedSdrObj();
        pInfo = SdDrawDocument::GetAnimationInfo(pObject);
        if( pInfo )
        {
            if( bActive != pInfo->mbActive )
                nAnimationSet = ATTR_MIXED;

            if( eEffect != pInfo->meEffect )
                nEffectSet = ATTR_MIXED;

            if( eTextEffect != pInfo->meTextEffect )
                nTextEffectSet = ATTR_MIXED;

            if( eSpeed != pInfo->meSpeed )
                nSpeedSet = ATTR_MIXED;

            if( bFadeOut != pInfo->mbDimPrevious )
                nFadeOutSet = ATTR_MIXED;

            if( aFadeColor != pInfo->maDimColor )
                nFadeColorSet = ATTR_MIXED;

            if( bInvisible != pInfo->mbDimHide )
                nInvisibleSet = ATTR_MIXED;

            if( bSoundOn != pInfo->mbSoundOn )
                nSoundOnSet = ATTR_MIXED;

            if( aSound != pInfo->maSoundFile )
                nSoundFileSet = ATTR_MIXED;

            if( bPlayFull != pInfo->mbPlayFull )
                nPlayFullSet = ATTR_MIXED;

            if( eClickAction != pInfo->meClickAction )
                nClickActionSet = ATTR_MIXED;

            if( aBookmark != pInfo->GetBookmark() )
                nBookmarkSet = ATTR_MIXED;

            if( eSecondEffect != pInfo->meSecondEffect )
                nSecondEffectSet = ATTR_MIXED;

            if( eSecondSpeed != pInfo->meSecondSpeed )
                nSecondSpeedSet = ATTR_MIXED;

            if( bSecondSoundOn != pInfo->mbSecondSoundOn )
                nSecondSoundOnSet = ATTR_MIXED;

            if( bSecondPlayFull != pInfo->mbSecondPlayFull )
                nSecondPlayFullSet = ATTR_MIXED;
        }
        else
        {
            if (nAnimationSet == ATTR_SET && bActive)
                nAnimationSet = ATTR_MIXED;

            if (nEffectSet == ATTR_SET && eEffect != presentation::AnimationEffect_NONE)
                nEffectSet = ATTR_MIXED;

            if (nTextEffectSet == ATTR_SET && eTextEffect != presentation::AnimationEffect_NONE)
                nTextEffectSet = ATTR_MIXED;

            if (nSpeedSet == ATTR_SET)
                nSpeedSet = ATTR_MIXED;

            if (nFadeOutSet == ATTR_SET && bFadeOut)
                nFadeOutSet = ATTR_MIXED;

            if (nFadeColorSet == ATTR_SET)
                nFadeColorSet = ATTR_MIXED;

            if (nInvisibleSet == ATTR_SET && bInvisible)
                nInvisibleSet = ATTR_MIXED;

            if (nSoundOnSet == ATTR_SET && bSoundOn)
                nSoundOnSet = ATTR_MIXED;

            if (nSoundFileSet == ATTR_SET)
                nSoundFileSet = ATTR_MIXED;

            if (nPlayFullSet == ATTR_SET && bPlayFull)
                nPlayFullSet = ATTR_MIXED;

            if (nClickActionSet == ATTR_SET && eClickAction != presentation::ClickAction_NONE)
                nClickActionSet = ATTR_MIXED;

            if (nBookmarkSet == ATTR_SET)
                nBookmarkSet = ATTR_MIXED;

            if (nSecondEffectSet == ATTR_SET && eSecondEffect != presentation::AnimationEffect_NONE)
                nSecondEffectSet = ATTR_MIXED;

            if (nSecondSpeedSet == ATTR_SET)
                nSecondSpeedSet = ATTR_MIXED;

            if (nSecondSoundOnSet == ATTR_SET && bSecondSoundOn)
                nSecondSoundOnSet = ATTR_MIXED;

            if (nSecondPlayFullSet == ATTR_SET && bSecondPlayFull)
                nSecondPlayFullSet = ATTR_MIXED;
        }
    }

    /* Exactly two objects with path effect?
       Then, only the animation info at the moved object is valid. */
    if (nCount == 2)
    {
        SdrObject* pObject1 = rMarkList.GetMark(0)->GetMarkedSdrObj();
        SdrObject* pObject2 = rMarkList.GetMark(1)->GetMarkedSdrObj();
        SdrObjKind eKind1   = pObject1->GetObjIdentifier();
        SdrObjKind eKind2   = pObject2->GetObjIdentifier();
        SdAnimationInfo* pInfo1 = SdDrawDocument::GetAnimationInfo(pObject1);
        SdAnimationInfo* pInfo2 = SdDrawDocument::GetAnimationInfo(pObject2);
        pInfo  = nullptr;

        if (pObject1->GetObjInventor() == SdrInventor::Default &&
            ((eKind1 == SdrObjKind::Line) ||                        // 2 point line
             (eKind1 == SdrObjKind::PolyLine) ||                        // Polygon
             (eKind1 == SdrObjKind::PathLine))                &&    // Bezier curve
             (pInfo2 && pInfo2->meEffect == presentation::AnimationEffect_PATH))
        {
            pInfo = pInfo2;
        }

        if (pObject2->GetObjInventor() == SdrInventor::Default &&
            ((eKind2 == SdrObjKind::Line) ||                        // 2 point line
             (eKind2 == SdrObjKind::PolyLine) ||                        // Polygon
             (eKind2 == SdrObjKind::PathLine))                &&    // Bezier curve
            (pInfo1 && pInfo1->meEffect == presentation::AnimationEffect_PATH))
        {
            pInfo = pInfo1;
        }

        if (pInfo)
        {
            bActive         = pInfo->mbActive;          nAnimationSet       = ATTR_SET;
            eEffect         = pInfo->meEffect;          nEffectSet          = ATTR_SET;
            eTextEffect     = pInfo->meTextEffect;      nTextEffectSet      = ATTR_SET;
            eSpeed          = pInfo->meSpeed;           nSpeedSet           = ATTR_SET;
            bFadeOut        = pInfo->mbDimPrevious;     nFadeOutSet         = ATTR_SET;
            aFadeColor      = pInfo->maDimColor;        nFadeColorSet       = ATTR_SET;
            bInvisible      = pInfo->mbDimHide;         nInvisibleSet       = ATTR_SET;
            bSoundOn        = pInfo->mbSoundOn;         nSoundOnSet         = ATTR_SET;
            aSound          = pInfo->maSoundFile;       nSoundFileSet       = ATTR_SET;
            bPlayFull       = pInfo->mbPlayFull;        nPlayFullSet        = ATTR_SET;
            eClickAction    = pInfo->meClickAction;     nClickActionSet     = ATTR_SET;
            aBookmark       = pInfo->GetBookmark();     nBookmarkSet        = ATTR_SET;
            eSecondEffect   = pInfo->meSecondEffect;    nSecondEffectSet    = ATTR_SET;
            eSecondSpeed    = pInfo->meSecondSpeed;     nSecondSpeedSet     = ATTR_SET;
            bSecondSoundOn  = pInfo->mbSecondSoundOn;   nSecondSoundOnSet   = ATTR_SET;
            bSecondPlayFull = pInfo->mbSecondPlayFull;  nSecondPlayFullSet  = ATTR_SET;
        }
    }

    const SfxItemSet* pArgs = rReq.GetArgs();

    if(!pArgs)
    {
        // fill ItemSet for dialog
        std::shared_ptr<SfxItemSet> aSet = std::make_shared<SfxItemSetFixed<ATTR_ANIMATION_START, ATTR_ACTION_END>>(mrDoc.GetPool());

        // fill the set
        if (nAnimationSet == ATTR_SET)
            aSet->Put( SfxBoolItem( ATTR_ANIMATION_ACTIVE, bActive));
        else if (nAnimationSet == ATTR_MIXED)
            aSet->InvalidateItem(ATTR_ANIMATION_ACTIVE);
        else
            aSet->Put(SfxBoolItem(ATTR_ANIMATION_ACTIVE, false));

        if (nEffectSet == ATTR_SET)
            aSet->Put(SfxUInt16Item(ATTR_ANIMATION_EFFECT, static_cast<sal_uInt16>(eEffect)));
        else if (nEffectSet == ATTR_MIXED)
            aSet->InvalidateItem( ATTR_ANIMATION_EFFECT );
        else
            aSet->Put(SfxUInt16Item(ATTR_ANIMATION_EFFECT, sal_uInt16(presentation::AnimationEffect_NONE)));

        if (nTextEffectSet == ATTR_SET)
            aSet->Put(SfxUInt16Item(ATTR_ANIMATION_TEXTEFFECT, static_cast<sal_uInt16>(eTextEffect)));
        else if (nTextEffectSet == ATTR_MIXED)
            aSet->InvalidateItem( ATTR_ANIMATION_TEXTEFFECT );
        else
            aSet->Put(SfxUInt16Item(ATTR_ANIMATION_TEXTEFFECT, sal_uInt16(presentation::AnimationEffect_NONE)));

        if (nSpeedSet == ATTR_SET)
            aSet->Put(SfxUInt16Item(ATTR_ANIMATION_SPEED, static_cast<sal_uInt16>(eSpeed)));
        else
            aSet->InvalidateItem(ATTR_ANIMATION_SPEED);

        if (nFadeOutSet == ATTR_SET)
            aSet->Put(SfxBoolItem(ATTR_ANIMATION_FADEOUT, bFadeOut));
        else if (nFadeOutSet == ATTR_MIXED)
            aSet->InvalidateItem(ATTR_ANIMATION_FADEOUT);
        else
            aSet->Put(SfxBoolItem(ATTR_ANIMATION_FADEOUT, false));

        if (nFadeColorSet == ATTR_SET)
            aSet->Put(SvxColorItem(aFadeColor, ATTR_ANIMATION_COLOR));
        else if (nFadeColorSet == ATTR_MIXED)
            aSet->InvalidateItem(ATTR_ANIMATION_COLOR);
        else
            aSet->Put(SvxColorItem(COL_LIGHTGRAY, ATTR_ANIMATION_COLOR));

        if (nInvisibleSet == ATTR_SET)
            aSet->Put(SfxBoolItem(ATTR_ANIMATION_INVISIBLE, bInvisible));
        else if (nInvisibleSet == ATTR_MIXED)
            aSet->InvalidateItem(ATTR_ANIMATION_INVISIBLE);
        else
            aSet->Put(SfxBoolItem(ATTR_ANIMATION_INVISIBLE, false));

        if (nSoundOnSet == ATTR_SET)
            aSet->Put(SfxBoolItem(ATTR_ANIMATION_SOUNDON, bSoundOn));
        else if (nSoundOnSet == ATTR_MIXED)
            aSet->InvalidateItem(ATTR_ANIMATION_SOUNDON);
        else
            aSet->Put(SfxBoolItem(ATTR_ANIMATION_SOUNDON, false));

        if (nSoundFileSet == ATTR_SET)
            aSet->Put(SfxStringItem(ATTR_ANIMATION_SOUNDFILE, aSound));
        else
            aSet->InvalidateItem(ATTR_ANIMATION_SOUNDFILE);

        if (nPlayFullSet == ATTR_SET)
            aSet->Put(SfxBoolItem(ATTR_ANIMATION_PLAYFULL, bPlayFull));
        else if (nPlayFullSet == ATTR_MIXED)
            aSet->InvalidateItem(ATTR_ANIMATION_PLAYFULL);
        else
            aSet->Put(SfxBoolItem(ATTR_ANIMATION_PLAYFULL, false));

        if (nClickActionSet == ATTR_SET)
            aSet->Put(SfxUInt16Item(ATTR_ACTION, static_cast<sal_uInt16>(eClickAction)));
        else if (nClickActionSet == ATTR_MIXED)
            aSet->InvalidateItem(ATTR_ACTION);
        else
            aSet->Put(SfxUInt16Item(ATTR_ACTION, sal_uInt16(presentation::ClickAction_NONE)));

        if (nBookmarkSet == ATTR_SET)
            aSet->Put(SfxStringItem(ATTR_ACTION_FILENAME, aBookmark));
        else
            aSet->InvalidateItem(ATTR_ACTION_FILENAME);

        if (nSecondEffectSet == ATTR_SET)
            aSet->Put(SfxUInt16Item(ATTR_ACTION_EFFECT, static_cast<sal_uInt16>(eSecondEffect)));
        else if (nSecondEffectSet == ATTR_MIXED)
            aSet->InvalidateItem( ATTR_ACTION_EFFECT );
        else
            aSet->Put(SfxUInt16Item(ATTR_ACTION_EFFECT, sal_uInt16(presentation::AnimationEffect_NONE)));

        if (nSecondSpeedSet == ATTR_SET)
            aSet->Put(SfxUInt16Item(ATTR_ACTION_EFFECTSPEED, static_cast<sal_uInt16>(eSecondSpeed)));
        else
            aSet->InvalidateItem(ATTR_ACTION_EFFECTSPEED);

        if (nSecondSoundOnSet == ATTR_SET)
            aSet->Put(SfxBoolItem(ATTR_ACTION_SOUNDON, bSecondSoundOn));
        else if (nSecondSoundOnSet == ATTR_MIXED)
            aSet->InvalidateItem(ATTR_ACTION_SOUNDON);
        else
            aSet->Put(SfxBoolItem(ATTR_ACTION_SOUNDON, false));

        if (nSecondPlayFullSet == ATTR_SET)
            aSet->Put(SfxBoolItem(ATTR_ACTION_PLAYFULL, bSecondPlayFull));
        else if (nSecondPlayFullSet == ATTR_MIXED)
            aSet->InvalidateItem(ATTR_ACTION_PLAYFULL);
        else
            aSet->Put(SfxBoolItem(ATTR_ACTION_PLAYFULL, false));

        std::shared_ptr<SfxRequest> xRequest = std::make_shared<SfxRequest>(rReq);
        rReq.Ignore(); // the 'old' request is not relevant any more

        SdAbstractDialogFactory* pFact = SdAbstractDialogFactory::Create();
        VclPtr<SfxAbstractDialog> pDlg( pFact->CreatSdActionDialog(mrViewShell.GetFrameWeld(), *aSet, mpView) );
        rtl::Reference<FuObjectAnimationParameters> xThis( this ); // avoid destruction within async processing
        pDlg->StartExecuteAsync([pDlg, xThis, xRequest=std::move(xRequest), aSet=std::move(aSet)](sal_Int32 nResult){
            if (nResult == RET_OK) {
                xThis->Finish(xRequest, pDlg);
            }
            pDlg->disposeOnce();
        });
    }
}

void FuObjectAnimationParameters::Finish( const std::shared_ptr<SfxRequest>& xRequest, const VclPtr<SfxAbstractDialog>& pDlg )
{
    SfxUndoManager* pUndoMgr = mrViewShell.GetViewFrame()->GetObjectShell()->GetUndoManager();

    const SdrMarkList& rMarkList  = mpView->GetMarkedObjectList();
    const size_t nCount = rMarkList.GetMarkCount();

    short nAnimationSet     = ATTR_MISSING;
    short nEffectSet        = ATTR_MISSING;
    short nTextEffectSet    = ATTR_MISSING;
    short nSpeedSet         = ATTR_MISSING;
    short nFadeColorSet     = ATTR_MISSING;
    short nFadeOutSet       = ATTR_MISSING;
    short nInvisibleSet     = ATTR_MISSING;
    short nSoundOnSet       = ATTR_MISSING;
    short nSoundFileSet     = ATTR_MISSING;
    short nPlayFullSet      = ATTR_MISSING;
    short nClickActionSet   = ATTR_MISSING;
    short nBookmarkSet      = ATTR_MISSING;

    short nSecondEffectSet      = ATTR_MISSING;
    short nSecondSpeedSet       = ATTR_MISSING;
    short nSecondSoundOnSet     = ATTR_MISSING;
    short nSecondPlayFullSet    = ATTR_MISSING;

    presentation::AnimationEffect eEffect         = presentation::AnimationEffect_NONE;
    presentation::AnimationEffect eTextEffect     = presentation::AnimationEffect_NONE;
    presentation::AnimationSpeed  eSpeed          = presentation::AnimationSpeed_MEDIUM;
    bool            bActive         = false;
    bool            bFadeOut        = false;
    Color           aFadeColor      = COL_LIGHTGRAY;
    bool            bInvisible      = false;
    bool            bSoundOn        = false;
    OUString        aSound;
    bool            bPlayFull       = false;
    presentation::ClickAction     eClickAction    = presentation::ClickAction_NONE;
    OUString        aBookmark;

    presentation::AnimationEffect eSecondEffect   = presentation::AnimationEffect_NONE;
    presentation::AnimationSpeed  eSecondSpeed    = presentation::AnimationSpeed_MEDIUM;
    bool            bSecondSoundOn  = false;
    bool            bSecondPlayFull = false;

    SdAnimationInfo* pInfo;

    xRequest->Done( *( pDlg->GetOutputItemSet() ) );
    auto pArgs = xRequest->GetArgs();

    // evaluation of the ItemSets
    if (pArgs->GetItemState(ATTR_ANIMATION_ACTIVE) == SfxItemState::SET)
    {
        bActive = static_cast<const SfxBoolItem&>(pArgs->Get(ATTR_ANIMATION_ACTIVE)).GetValue();
        nAnimationSet = ATTR_SET;
    }
    else
        nAnimationSet = ATTR_MISSING;

    if (pArgs->GetItemState(ATTR_ANIMATION_EFFECT) == SfxItemState::SET)
    {
        eEffect = static_cast<presentation::AnimationEffect>( pArgs->
                    Get(ATTR_ANIMATION_EFFECT).GetValue());
        nEffectSet = ATTR_SET;
    }
    else
        nEffectSet = ATTR_MISSING;

    if (pArgs->GetItemState(ATTR_ANIMATION_TEXTEFFECT) == SfxItemState::SET)
    {
        eTextEffect = static_cast<presentation::AnimationEffect>(static_cast<const SfxUInt16Item&>( pArgs->
                        Get(ATTR_ANIMATION_TEXTEFFECT)).GetValue());
        nTextEffectSet = ATTR_SET;
    }
    else
        nTextEffectSet = ATTR_MISSING;

    if (pArgs->GetItemState(ATTR_ANIMATION_SPEED) == SfxItemState::SET)
    {
        eSpeed  = static_cast<presentation::AnimationSpeed>( pArgs->
                    Get(ATTR_ANIMATION_SPEED).GetValue());
        nSpeedSet = ATTR_SET;
    }
    else
        nSpeedSet = ATTR_MISSING;

    if (pArgs->GetItemState(ATTR_ANIMATION_FADEOUT) == SfxItemState::SET)
    {
        bFadeOut = static_cast<const SfxBoolItem&>(pArgs->Get(ATTR_ANIMATION_FADEOUT)).GetValue();
        nFadeOutSet = ATTR_SET;
    }
    else
        nFadeOutSet = ATTR_MISSING;

    if (pArgs->GetItemState(ATTR_ANIMATION_INVISIBLE) == SfxItemState::SET)
    {
        bInvisible = static_cast<const SfxBoolItem&>(pArgs->Get(ATTR_ANIMATION_INVISIBLE)).GetValue();
        nInvisibleSet = ATTR_SET;
    }
    else
        nInvisibleSet = ATTR_MISSING;

    if (pArgs->GetItemState(ATTR_ANIMATION_SOUNDON) == SfxItemState::SET)
    {
        bSoundOn = static_cast<const SfxBoolItem&>(pArgs->Get(ATTR_ANIMATION_SOUNDON)).GetValue();
        nSoundOnSet = ATTR_SET;
    }
    else
        nSoundOnSet = ATTR_MISSING;

    if (pArgs->GetItemState(ATTR_ANIMATION_SOUNDFILE) == SfxItemState::SET)
    {
        aSound = pArgs->Get(ATTR_ANIMATION_SOUNDFILE).GetValue();
        nSoundFileSet = ATTR_SET;
    }
    else
        nSoundFileSet = ATTR_MISSING;

    if (pArgs->GetItemState(ATTR_ANIMATION_COLOR) == SfxItemState::SET)
    {
        aFadeColor = static_cast<const SvxColorItem&>(pArgs->Get(ATTR_ANIMATION_COLOR)).GetValue();
        nFadeColorSet = ATTR_SET;
    }
    else
        nFadeColorSet = ATTR_MISSING;

    if (pArgs->GetItemState(ATTR_ANIMATION_PLAYFULL) == SfxItemState::SET)
    {
        bPlayFull = static_cast<const SfxBoolItem&>(pArgs->Get(ATTR_ANIMATION_PLAYFULL)).GetValue();
        nPlayFullSet = ATTR_SET;
    }
    else
        nPlayFullSet = ATTR_MISSING;

    if (pArgs->GetItemState(ATTR_ACTION) == SfxItemState::SET)
    {
        eClickAction = static_cast<presentation::ClickAction>(pArgs->
                    Get(ATTR_ACTION).GetValue());
        nClickActionSet = ATTR_SET;
    }
    else
        nClickActionSet = ATTR_MISSING;

    if (pArgs->GetItemState(ATTR_ACTION_FILENAME) == SfxItemState::SET)
    {
        aBookmark = pArgs->Get(ATTR_ACTION_FILENAME).GetValue();
        nBookmarkSet = ATTR_SET;
    }
    else
        nBookmarkSet = ATTR_MISSING;

    if (pArgs->GetItemState(ATTR_ACTION_EFFECT) == SfxItemState::SET)
    {
        eSecondEffect = static_cast<presentation::AnimationEffect>( pArgs->
                    Get(ATTR_ACTION_EFFECT).GetValue());
        nSecondEffectSet = ATTR_SET;
    }
    else
        nSecondEffectSet = ATTR_MISSING;

    if (pArgs->GetItemState(ATTR_ACTION_EFFECTSPEED) == SfxItemState::SET)
    {
        eSecondSpeed  = static_cast<presentation::AnimationSpeed>( pArgs->
                    Get(ATTR_ACTION_EFFECTSPEED).GetValue());
        nSecondSpeedSet = ATTR_SET;
    }
    else
        nSecondSpeedSet = ATTR_MISSING;

    if (pArgs->GetItemState(ATTR_ACTION_SOUNDON) == SfxItemState::SET)
    {
        bSecondSoundOn = static_cast<const SfxBoolItem&>(pArgs->Get(ATTR_ACTION_SOUNDON)).GetValue();
        nSecondSoundOnSet = ATTR_SET;
    }
    else
        nSecondSoundOnSet = ATTR_MISSING;

    if (pArgs->GetItemState(ATTR_ACTION_PLAYFULL) == SfxItemState::SET)
    {
        bSecondPlayFull = static_cast<const SfxBoolItem&>(pArgs->Get(ATTR_ACTION_PLAYFULL)).GetValue();
        nSecondPlayFullSet = ATTR_SET;
    }
    else
        nSecondPlayFullSet = ATTR_MISSING;

    // if any attribute is chosen
    if (!(nEffectSet         == ATTR_SET  ||
        nTextEffectSet     == ATTR_SET  ||
        nSpeedSet          == ATTR_SET  ||
        nAnimationSet      == ATTR_SET  ||
        nFadeOutSet        == ATTR_SET  ||
        nFadeColorSet      == ATTR_SET  ||
        nInvisibleSet      == ATTR_SET  ||
        nSoundOnSet        == ATTR_SET  ||
        nSoundFileSet      == ATTR_SET  ||
        nPlayFullSet       == ATTR_SET  ||
        nClickActionSet    == ATTR_SET  ||
        nBookmarkSet       == ATTR_SET  ||
        nSecondEffectSet   == ATTR_SET  ||
        nSecondSpeedSet    == ATTR_SET  ||
        nSecondSoundOnSet  == ATTR_SET  ||
        nSecondPlayFullSet == ATTR_SET))
        return;

    // String for undo-group and list-action
    OUString aComment(SdResId(STR_UNDO_ANIMATION));

    // with 'following curves', we have an additional UndoAction
    // therefore cling? here
    pUndoMgr->EnterListAction(aComment, aComment, 0, mrViewShell.GetViewShellBase().GetViewShellId());

    // create undo group
    std::unique_ptr<SdUndoGroup> pUndoGroup(new SdUndoGroup(mrDoc));
    pUndoGroup->SetComment(aComment);

    // for the path effect, remember some stuff
    SdrPathObj* pPath       = nullptr;
    if (eEffect == presentation::AnimationEffect_PATH && nEffectSet == ATTR_SET)
    {
        DBG_ASSERT(nCount == 2, "This effect expects two selected objects");
        SdrObject* pObject1 = rMarkList.GetMark(0)->GetMarkedSdrObj();
        SdrObject* pObject2 = rMarkList.GetMark(1)->GetMarkedSdrObj();
        SdrObjKind eKind1   = pObject1->GetObjIdentifier();
        SdrObjKind eKind2   = pObject2->GetObjIdentifier();
        SdrObject* pRunningObj = nullptr;

        if (pObject1->GetObjInventor() == SdrInventor::Default &&
            ((eKind1 == SdrObjKind::Line) ||        // 2 point line
             (eKind1 == SdrObjKind::PolyLine) ||        // Polygon
             (eKind1 == SdrObjKind::PathLine)))     // Bezier curve
        {
            pPath = static_cast<SdrPathObj*>(pObject1);
            pRunningObj = pObject2;
        }

        if (pObject2->GetObjInventor() == SdrInventor::Default &&
            ((eKind2 == SdrObjKind::Line) ||        // 2 point line
             (eKind2 == SdrObjKind::PolyLine) ||        // Polygon
             (eKind2 == SdrObjKind::PathLine)))     // Bezier curve
        {
            pPath = static_cast<SdrPathObj*>(pObject2);
            pRunningObj = pObject1;
        }

        assert(pRunningObj && pPath && "no curve found");

        // push the running object to the end of the curve
        if (pRunningObj)
        {
            ::tools::Rectangle aCurRect(pRunningObj->GetLogicRect());
            Point     aCurCenter(aCurRect.Center());
            const ::basegfx::B2DPolyPolygon& rPolyPolygon = pPath->GetPathPoly();
            sal_uInt32 nNoOfPolygons(rPolyPolygon.count());
            const ::basegfx::B2DPolygon& aPolygon(rPolyPolygon.getB2DPolygon(nNoOfPolygons - 1));
            sal_uInt32 nPoints(aPolygon.count());
            const ::basegfx::B2DPoint aNewB2DCenter(aPolygon.getB2DPoint(nPoints - 1));
            const Point aNewCenter(basegfx::fround<::tools::Long>(aNewB2DCenter.getX()),
                                   basegfx::fround<::tools::Long>(aNewB2DCenter.getY()));
            Size aDistance(aNewCenter.X() - aCurCenter.X(), aNewCenter.Y() - aCurCenter.Y());
            pRunningObj->Move(aDistance);

            pUndoMgr->AddUndoAction(mrDoc.GetSdrUndoFactory().CreateUndoMoveObject( *pRunningObj, aDistance));
        }
    }

    for (size_t nObject = 0; nObject < nCount; ++nObject)
    {
        SdrObject* pObject = rMarkList.GetMark(nObject)->GetMarkedSdrObj();

        pInfo = SdDrawDocument::GetAnimationInfo(pObject);

        bool bCreated = false;
        if( !pInfo )
        {
            pInfo = SdDrawDocument::GetShapeUserData(*pObject,true);
            bCreated = true;
        }

        // path object for 'following curves'?
        if (eEffect == presentation::AnimationEffect_PATH && pObject == pPath)
        {
            SdAnimationPrmsUndoAction* pAction = new SdAnimationPrmsUndoAction
                                            (mrDoc, pObject, bCreated);
            pAction->SetActive(pInfo->mbActive, pInfo->mbActive);
            pAction->SetEffect(pInfo->meEffect, pInfo->meEffect);
            pAction->SetTextEffect(pInfo->meTextEffect, pInfo->meTextEffect);
            pAction->SetSpeed(pInfo->meSpeed, pInfo->meSpeed);
            pAction->SetDim(pInfo->mbDimPrevious, pInfo->mbDimPrevious);
            pAction->SetDimColor(pInfo->maDimColor, pInfo->maDimColor);
            pAction->SetDimHide(pInfo->mbDimHide, pInfo->mbDimHide);
            pAction->SetSoundOn(pInfo->mbSoundOn, pInfo->mbSoundOn);
            pAction->SetSound(pInfo->maSoundFile, pInfo->maSoundFile);
            pAction->SetPlayFull(pInfo->mbPlayFull, pInfo->mbPlayFull);
            pAction->SetClickAction(pInfo->meClickAction, pInfo->meClickAction);
            pAction->SetBookmark(pInfo->GetBookmark(), pInfo->GetBookmark());
            pAction->SetVerb(pInfo->mnVerb, pInfo->mnVerb);
            pAction->SetSecondEffect(pInfo->meSecondEffect, pInfo->meSecondEffect);
            pAction->SetSecondSpeed(pInfo->meSecondSpeed, pInfo->meSecondSpeed);
            pAction->SetSecondSoundOn(pInfo->mbSecondSoundOn, pInfo->mbSecondSoundOn);
            pAction->SetSecondPlayFull(pInfo->mbSecondPlayFull, pInfo->mbSecondPlayFull);
            pUndoGroup->AddAction(pAction);

        }
        else
        {

            // create undo action with old and new sizes
            SdAnimationPrmsUndoAction* pAction = new SdAnimationPrmsUndoAction
                                            (mrDoc, pObject, bCreated);
            pAction->SetActive(pInfo->mbActive, bActive);
            pAction->SetEffect(pInfo->meEffect, eEffect);
            pAction->SetTextEffect(pInfo->meTextEffect, eTextEffect);
            pAction->SetSpeed(pInfo->meSpeed, eSpeed);
            pAction->SetDim(pInfo->mbDimPrevious, bFadeOut);
            pAction->SetDimColor(pInfo->maDimColor, aFadeColor);
            pAction->SetDimHide(pInfo->mbDimHide, bInvisible);
            pAction->SetSoundOn(pInfo->mbSoundOn, bSoundOn);
            pAction->SetSound(pInfo->maSoundFile, aSound);
            pAction->SetPlayFull(pInfo->mbPlayFull, bPlayFull);
            pAction->SetClickAction(pInfo->meClickAction, eClickAction);
            pAction->SetBookmark(pInfo->GetBookmark(), aBookmark);
            pAction->SetVerb(pInfo->mnVerb, static_cast<sal_uInt16>(pInfo->GetBookmark().toInt32()) );
            pAction->SetSecondEffect(pInfo->meSecondEffect, eSecondEffect);
            pAction->SetSecondSpeed(pInfo->meSecondSpeed, eSecondSpeed);
            pAction->SetSecondSoundOn(pInfo->mbSecondSoundOn, bSecondSoundOn);
            pAction->SetSecondPlayFull(pInfo->mbSecondPlayFull,bSecondPlayFull);
            pUndoGroup->AddAction(pAction);

            // insert new values at info block of the object
            if (nAnimationSet == ATTR_SET)
                pInfo->mbActive = bActive;

            if (nEffectSet == ATTR_SET)
                pInfo->meEffect = eEffect;

            if (nTextEffectSet == ATTR_SET)
                pInfo->meTextEffect = eTextEffect;

            if (nSpeedSet == ATTR_SET)
                pInfo->meSpeed = eSpeed;

            if (nFadeOutSet == ATTR_SET)
                pInfo->mbDimPrevious = bFadeOut;

            if (nFadeColorSet == ATTR_SET)
                pInfo->maDimColor = aFadeColor;

            if (nInvisibleSet == ATTR_SET)
                pInfo->mbDimHide = bInvisible;

            if (nSoundOnSet == ATTR_SET)
                pInfo->mbSoundOn = bSoundOn;

            if (nSoundFileSet == ATTR_SET)
                pInfo->maSoundFile = aSound;

            if (nPlayFullSet == ATTR_SET)
                pInfo->mbPlayFull = bPlayFull;

            if (nClickActionSet == ATTR_SET)
                pInfo->meClickAction = eClickAction;

            if (nBookmarkSet == ATTR_SET)
                pInfo->SetBookmark( aBookmark );

            if (nSecondEffectSet == ATTR_SET)
                pInfo->meSecondEffect = eSecondEffect;

            if (nSecondSpeedSet == ATTR_SET)
                pInfo->meSecondSpeed = eSecondSpeed;

            if (nSecondSoundOnSet == ATTR_SET)
                pInfo->mbSecondSoundOn = bSecondSoundOn;

            if (nSecondPlayFullSet == ATTR_SET)
                pInfo->mbSecondPlayFull = bSecondPlayFull;

            if (eClickAction == presentation::ClickAction_VERB)
                pInfo->mnVerb = static_cast<sal_uInt16>(aBookmark.toInt32());
        }
    }
    // Set the Undo Group in of the Undo Manager
    pUndoMgr->AddUndoAction(std::move(pUndoGroup));
    pUndoMgr->LeaveListAction();

    // Model changed
    mrDoc.SetChanged();
    // not seen, therefore we do not need to invalidate at the bindings
}

} // end of namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
