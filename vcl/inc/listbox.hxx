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

#pragma once

#include <sal/config.h>

#include <o3tl/safeint.hxx>
#include <utility>
#include <vcl/glyphitem.hxx>
#include <vcl/toolkit/button.hxx>
#include <vcl/toolkit/floatwin.hxx>
#include <vcl/toolkit/lstbox.hxx>
#include <vcl/quickselectionengine.hxx>

#include <set>
#include <vector>
#include <memory>

class ScrollBar;
class ScrollBarBox;

#define HORZ_SCROLL         4
#define IMG_TXT_DISTANCE    6

enum LB_EVENT_TYPE
{
    LET_MBDOWN,
    LET_TRACKING,
    LET_KEYMOVE,
    LET_KEYSPACE
};

struct ImplEntryType
{
    OUString    maStr;
    SalLayoutGlyphs maStrGlyphs;
    Image       maImage;
    void*       mpUserData;
    bool        mbIsSelected;
    ListBoxEntryFlags mnFlags;
    tools::Long        mnHeight;

    tools::Long getHeightWithMargin() const;

    ImplEntryType( OUString aStr, Image aImage ) :
        maStr(std::move( aStr )),
        maImage(std::move( aImage )),
        mnFlags( ListBoxEntryFlags::NONE ),
        mnHeight( 0 )
    {
        mbIsSelected = false;
        mpUserData = nullptr;
    }

    ImplEntryType( OUString aStr ) :
        maStr(std::move( aStr )),
        mnFlags( ListBoxEntryFlags::NONE ),
        mnHeight( 0 )
    {
        mbIsSelected = false;
        mpUserData = nullptr;
    }

    /// Computes maStr's text layout (glyphs), cached in maStrGlyphs.
    SalLayoutGlyphs* GetTextGlyphs(const OutputDevice* pOutputDevice);
};

class ImplEntryList
{
private:
    VclPtr<vcl::Window> mpWindow;   ///< For getting the current locale when matching strings
    sal_Int32       mnLastSelected;
    sal_Int32       mnSelectionAnchor;
    sal_Int32       mnImages;

    sal_Int32       mnMRUCount;
    sal_Int32       mnMaxMRUCount;

    Link<sal_Int32,void> maSelectionChangedHdl;
    bool            mbCallSelectionChangedHdl;
    std::vector<std::unique_ptr<ImplEntryType> > maEntries;

    ImplEntryType*  GetEntry( sal_Int32  nPos ) const
    {
        if (nPos < 0 || o3tl::make_unsigned(nPos) >= maEntries.size())
            return nullptr;
        return maEntries[nPos].get();
    }

public:
                    ImplEntryList( vcl::Window* pWindow );
                    ~ImplEntryList();

    sal_Int32               InsertEntry( sal_Int32  nPos, ImplEntryType* pNewEntry, bool bSort );
    void                    RemoveEntry( sal_Int32  nPos );
    const ImplEntryType*    GetEntryPtr( sal_Int32  nPos ) const { return GetEntry( nPos ); }
    ImplEntryType*          GetMutableEntryPtr( sal_Int32  nPos ) const { return GetEntry( nPos ); }
    void                    Clear();
    void                    dispose();

    sal_Int32           FindMatchingEntry( const OUString& rStr, sal_Int32  nStart, bool bLazy ) const;
    sal_Int32           FindEntry( std::u16string_view rStr, bool bSearchMRUArea = false ) const;

    /// helper: add up heights up to index nEndIndex.
    /// GetAddedHeight( 0 ) @return 0
    /// GetAddedHeight( LISTBOX_ENTRY_NOTFOUND ) @return 0
    /// GetAddedHeight( i, k ) with k > i is equivalent -GetAddedHeight( k, i )
    tools::Long            GetAddedHeight( sal_Int32  nEndIndex, sal_Int32  nBeginIndex ) const;
    tools::Long            GetEntryHeight( sal_Int32  nPos ) const;

    sal_Int32       GetEntryCount() const { return static_cast<sal_Int32>(maEntries.size()); }
    bool            HasImages() const { return mnImages != 0; }

    OUString        GetEntryText( sal_Int32  nPos ) const;

    bool            HasEntryImage( sal_Int32  nPos ) const;
    Image           GetEntryImage( sal_Int32  nPos ) const;

    void            SetEntryData( sal_Int32  nPos, void* pNewData );
    void*           GetEntryData( sal_Int32  nPos ) const;

    void              SetEntryFlags( sal_Int32  nPos, ListBoxEntryFlags nFlags );

    void            SelectEntry( sal_Int32  nPos, bool bSelect );

    sal_Int32       GetSelectedEntryCount() const;
    OUString        GetSelectedEntry( sal_Int32  nIndex ) const;
    sal_Int32       GetSelectedEntryPos( sal_Int32  nIndex ) const;
    bool            IsEntryPosSelected( sal_Int32  nIndex ) const;

    void            SetLastSelected( sal_Int32  nPos )  { mnLastSelected = nPos; }
    sal_Int32       GetLastSelected() const { return mnLastSelected; }

    void            SetSelectionAnchor( sal_Int32  nPos )   { mnSelectionAnchor = nPos; }
    sal_Int32       GetSelectionAnchor() const { return mnSelectionAnchor; }

    void            SetSelectionChangedHdl( const Link<sal_Int32,void>& rLnk ) { maSelectionChangedHdl = rLnk; }
    void            SetCallSelectionChangedHdl( bool bCall )    { mbCallSelectionChangedHdl = bCall; }

    void            SetMRUCount( sal_Int32  n ) { mnMRUCount = n; }
    sal_Int32       GetMRUCount() const     { return mnMRUCount; }

    void            SetMaxMRUCount( sal_Int32  n )  { mnMaxMRUCount = n; }
    sal_Int32       GetMaxMRUCount() const      { return mnMaxMRUCount; }

    /** An Entry is selectable if its mnFlags does not have the
        ListBoxEntryFlags::DisableSelection flag set. */
    bool            IsEntrySelectable( sal_Int32  nPos ) const;

    /** @return the first entry found from the given position nPos that is selectable
        or LISTBOX_ENTRY_NOTFOUND if non is found. If the entry at nPos is not selectable,
        it returns the first selectable entry after nPos if bForward is true and the
        first selectable entry after nPos is bForward is false.
        */
    sal_Int32       FindFirstSelectable( sal_Int32  nPos, bool bForward = true ) const;
};

class ImplListBoxWindow final : public Control, public vcl::ISearchableStringList
{
private:
    ImplEntryList maEntryList;     ///< EntryList
    tools::Rectangle       maFocusRect;

    Size            maUserItemSize;

    tools::Long            mnMaxTxtHeight;  ///< Maximum height of a text item
    tools::Long            mnMaxTxtWidth;   ///< Maximum width of a text item
                                     ///< Entry without Image
    tools::Long            mnMaxImgTxtWidth;///< Maximum width of a text item
                                     ///< Entry AND Image
    tools::Long            mnMaxImgWidth;   ///< Maximum width of an image item
    tools::Long            mnMaxImgHeight;  ///< Maximum height of an image item
    tools::Long            mnMaxWidth;      ///< Maximum width of an entry
    tools::Long            mnMaxHeight;     ///< Maximum height of an entry

    sal_Int32       mnCurrentPos;    ///< Position (Focus)
    sal_Int32       mnTrackingSaveSelection; ///< Selection before Tracking();

    std::set< sal_Int32 > maSeparators; ///< Separator positions

    sal_Int32       mnUserDrawEntry;

    sal_Int32       mnTop;           ///< output from line on
    tools::Long            mnLeft;          ///< output from column on
    tools::Long            mnTextHeight;    ///< text height

    sal_uInt16      mnSelectModifier;   ///< Modifiers

    bool mbHasFocusRect : 1;
    bool mbSort : 1;             ///< ListBox sorted
    bool mbTrack : 1;            ///< Tracking
    bool mbMulti : 1;            ///< MultiListBox
    bool mbSimpleMode : 1;       ///< SimpleMode for MultiListBox
    bool mbTravelSelect : 1;     ///< TravelSelect
    bool mbTrackingSelect : 1;   ///< Selected at a MouseMove
    bool mbSelectionChanged : 1; ///< Do not call Select() too often ...
    bool mbMouseMoveSelect : 1;  ///< Select at MouseMove
    bool mbGrabFocus : 1;        ///< Grab focus at MBDown
    bool mbUserDrawEnabled : 1;  ///< UserDraw possible
    bool mbInUserDraw : 1;       ///< In UserDraw
    bool mbReadOnly : 1;         ///< ReadOnly
    bool mbCenter : 1;           ///< center Text output
    bool mbRight : 1;            ///< right align Text output
    bool mbEdgeBlending : 1;
    /// Listbox is actually a dropdown (either combobox, or popup window treated as dropdown)
    bool mbIsDropdown : 1;

    Point mnLastPosPixel = Point();

    Link<ImplListBoxWindow*,void>  maScrollHdl;
    Link<LinkParamNone*,void>      maSelectHdl;
    Link<LinkParamNone*,void>      maCancelHdl;
    Link<ImplListBoxWindow*,void>  maDoubleClickHdl;
    Link<UserDrawEvent*, void>     maUserDrawHdl;
    Link<LinkParamNone*,void>      maMRUChangedHdl;
    Link<sal_Int32,void>           maFocusHdl;
    Link<LinkParamNone*,void>      maListItemSelectHdl;

    vcl::QuickSelectionEngine maQuickSelectionEngine;

    virtual void    KeyInput( const KeyEvent& rKEvt ) override;
    virtual void    MouseButtonDown( const MouseEvent& rMEvt ) override;
    virtual void    MouseMove( const MouseEvent& rMEvt ) override;
    virtual void    Tracking( const TrackingEvent& rTEvt ) override;
    virtual void    Paint(vcl::RenderContext& rRenderContext, const tools::Rectangle& rRect) override;
    virtual void    Resize() override;
    virtual void    GetFocus() override;
    virtual void    LoseFocus() override;

    bool            SelectEntries( sal_Int32  nSelect, LB_EVENT_TYPE eLET, bool bShift = false, bool bCtrl = false, bool bSelectPosChange = false );
    void            ImplPaint(vcl::RenderContext& rRenderContext, sal_Int32 nPos);
    void            ImplDoPaint(vcl::RenderContext& rRenderContext, const tools::Rectangle& rRect);
    void            ImplCalcMetrics();
    void            ImplUpdateEntryMetrics( ImplEntryType& rEntry );
    void            ImplCallSelect();

    void            ImplShowFocusRect();
    void            ImplHideFocusRect();

    virtual void    StateChanged( StateChangedType nType ) override;
    virtual void    DataChanged( const DataChangedEvent& rDCEvt ) override;

public:
    virtual void  FillLayoutData() const override;

                    ImplListBoxWindow( vcl::Window* pParent, WinBits nWinStyle );
    virtual         ~ImplListBoxWindow() override;
    virtual void    dispose() override;

    const ImplEntryList& GetEntryList() const { return maEntryList; }
    ImplEntryList& GetEntryList() { return maEntryList; }

    sal_Int32       InsertEntry( sal_Int32  nPos, ImplEntryType* pNewEntry ); // sorts using mbSort
    sal_Int32       InsertEntry( sal_Int32  nPos, ImplEntryType* pNewEntry, bool bSort ); // to insert ignoring mbSort, e.g. mru
    void            RemoveEntry( sal_Int32  nPos );
    void            Clear();
    void            ResetCurrentPos()               { mnCurrentPos = LISTBOX_ENTRY_NOTFOUND; }
    sal_Int32       GetCurrentPos() const           { return mnCurrentPos; }
    sal_uInt16      GetDisplayLineCount() const;
    void            SetEntryFlags( sal_Int32  nPos, ListBoxEntryFlags nFlags );

    void            DrawEntry(vcl::RenderContext& rRenderContext, sal_Int32  nPos, bool bDrawImage, bool bDrawText);

    void            SelectEntry( sal_Int32  nPos, bool bSelect );
    void            DeselectAll();
    sal_Int32       GetEntryPosForPoint( const Point& rPoint ) const;
    sal_Int32       GetLastVisibleEntry() const;

    bool            ProcessKeyInput( const KeyEvent& rKEvt );

    void            SetTopEntry( sal_Int32  nTop );
    sal_Int32       GetTopEntry() const             { return mnTop; }
    /** ShowProminentEntry will set the entry corresponding to nEntryPos
        either at top or in the middle depending on the chosen style*/
    void            ShowProminentEntry( sal_Int32  nEntryPos );
    using Window::IsVisible;
    bool            IsVisible( sal_Int32  nEntry ) const;

    tools::Long            GetLeftIndent() const           { return mnLeft; }
    void            SetLeftIndent( tools::Long n );
    void            ScrollHorz( tools::Long nDiff );

    void            AllowGrabFocus( bool b )        { mbGrabFocus = b; }
    bool            IsGrabFocusAllowed() const      { return mbGrabFocus; }

    /**
     * Removes existing separators, and sets the position of the
     * one and only separator.
     */
    void            SetSeparatorPos( sal_Int32  n );
    /**
     * Gets the position of the separator which was added first.
     * Returns LISTBOX_ENTRY_NOTFOUND if there is no separator.
     */
    sal_Int32       GetSeparatorPos() const;

    /**
     * Adds a new separator at the given position n.
     */
    void            AddSeparator( sal_Int32 n )     { maSeparators.insert( n ); }
    /**
     * Checks if the given number n is an element of the separator positions set.
     */
    bool            isSeparator( const sal_Int32 &n ) const;

    void            SetTravelSelect( bool bTravelSelect ) { mbTravelSelect = bTravelSelect; }
    bool            IsTravelSelect() const          { return mbTravelSelect; }
    bool            IsTrackingSelect() const        { return mbTrackingSelect; }

    void            SetUserItemSize( const Size& rSz );

    void            EnableUserDraw( bool bUserDraw ) { mbUserDrawEnabled = bUserDraw; }
    bool            IsUserDrawEnabled() const   { return mbUserDrawEnabled; }

    void            EnableMultiSelection( bool bMulti ) { mbMulti = bMulti; }
    bool            IsMultiSelectionEnabled() const     { return mbMulti; }

    void            SetMultiSelectionSimpleMode( bool bSimple ) { mbSimpleMode = bSimple; }

    void            EnableMouseMoveSelect( bool bMouseMoveSelect ) { mbMouseMoveSelect = bMouseMoveSelect; }
    bool            IsMouseMoveSelect() const   { return mbMouseMoveSelect; }

    Size            CalcSize(sal_Int32 nMaxLines) const;
    tools::Rectangle       GetBoundingRectangle( sal_Int32  nItem ) const;

    tools::Long            GetEntryHeight() const              { return mnMaxHeight; }
    tools::Long            GetEntryHeightWithMargin() const;
    tools::Long            GetMaxEntryWidth() const            { return mnMaxWidth; }

    void            SetScrollHdl( const Link<ImplListBoxWindow*,void>& rLink ) { maScrollHdl = rLink; }
    void            SetSelectHdl( const Link<LinkParamNone*,void>& rLink ) { maSelectHdl = rLink; }
    void            SetCancelHdl( const Link<LinkParamNone*,void>& rLink ) { maCancelHdl = rLink; }
    void            SetDoubleClickHdl( const Link<ImplListBoxWindow*,void>& rLink ) { maDoubleClickHdl = rLink; }
    void            SetUserDrawHdl( const Link<UserDrawEvent*, void>& rLink ) { maUserDrawHdl = rLink; }
    void            SetMRUChangedHdl( const Link<LinkParamNone*,void>& rLink ) { maMRUChangedHdl = rLink; }
    void            SetFocusHdl( const Link<sal_Int32,void>& rLink )  { maFocusHdl = rLink ; }

    void            SetListItemSelectHdl( const Link<LinkParamNone*,void>& rLink ) { maListItemSelectHdl = rLink ; }
    bool            IsSelectionChanged() const { return mbSelectionChanged; }
    sal_uInt16      GetSelectModifier() const { return mnSelectModifier; }

    void            EnableSort( bool b ) { mbSort = b; }

    void            SetReadOnly( bool bReadOnly )   { mbReadOnly = bReadOnly; }
    bool            IsReadOnly() const              { return mbReadOnly; }

    DrawTextFlags   ImplGetTextStyle() const;

    bool GetEdgeBlending() const { return mbEdgeBlending; }
    void SetEdgeBlending(bool bNew) { mbEdgeBlending = bNew; }

    using Control::ImplInitSettings;
    virtual void ApplySettings(vcl::RenderContext& rRenderContext) override;

    void ResetLastPosPixel() { mnLastPosPixel = Point(); }

private:
    // ISearchableStringList
    virtual vcl::StringEntryIdentifier    CurrentEntry( OUString& _out_entryText ) const override;
    virtual vcl::StringEntryIdentifier    NextEntry( vcl::StringEntryIdentifier _currentEntry, OUString& _out_entryText ) const override;
    virtual void                            SelectEntry( vcl::StringEntryIdentifier _entry ) override;
};

class ImplListBox final : public Control
{
private:
    VclPtr<ImplListBoxWindow> maLBWindow;
    VclPtr<ScrollBar>    mpHScrollBar;
    VclPtr<ScrollBar>    mpVScrollBar;
    VclPtr<ScrollBarBox> mpScrollBarBox;

    bool mbVScroll : 1;     // VScroll on or off
    bool mbHScroll : 1;     // HScroll on or off
    bool mbAutoHScroll : 1; // AutoHScroll on or off
    bool mbEdgeBlending : 1;

    Link<ImplListBox*,void>   maScrollHdl;    // because it is needed by ImplListBoxWindow itself

    virtual void        GetFocus() override;
    virtual void        StateChanged( StateChangedType nType ) override;

    virtual bool        EventNotify( NotifyEvent& rNEvt ) override;

    void                ImplResizeControls();
    void                ImplCheckScrollBars();
    void                ImplInitScrollBars();

    DECL_LINK(    ScrollBarHdl, ScrollBar*, void );
    DECL_LINK(    LBWindowScrolled, ImplListBoxWindow*, void );
    DECL_LINK(    MRUChanged, LinkParamNone*, void );

public:
                    ImplListBox( vcl::Window* pParent, WinBits nWinStyle );
                    virtual ~ImplListBox() override;
    virtual void    dispose() override;

    const ImplEntryList&    GetEntryList() const            { return maLBWindow->GetEntryList(); }
    ImplListBoxWindow*      GetMainWindow()                 { return maLBWindow.get(); }

    virtual void    Resize() override;
    virtual const Wallpaper& GetDisplayBackground() const override;

    sal_Int32       InsertEntry( sal_Int32  nPos, const OUString& rStr );
    sal_Int32       InsertEntry( sal_Int32  nPos, const OUString& rStr, const Image& rImage );
    void            RemoveEntry( sal_Int32  nPos );
    void            SetEntryData( sal_Int32  nPos, void* pNewData ) { maLBWindow->GetEntryList().SetEntryData( nPos, pNewData ); }
    void            Clear();

    void            SetEntryFlags( sal_Int32  nPos, ListBoxEntryFlags nFlags );

    void            SelectEntry( sal_Int32  nPos, bool bSelect );
    void            SetNoSelection();
    void            ResetCurrentPos()               { maLBWindow->ResetCurrentPos(); }
    sal_Int32       GetCurrentPos() const           { return maLBWindow->GetCurrentPos(); }

    bool            ProcessKeyInput( const KeyEvent& rKEvt )    { return maLBWindow->ProcessKeyInput( rKEvt ); }
    bool            HandleWheelAsCursorTravel(const CommandEvent& rCEvt, Control& rControl);

    /**
     * Removes existing separators, and sets the position of the
     * one and only separator.
     */
    void            SetSeparatorPos( sal_Int32  n )     { maLBWindow->SetSeparatorPos( n ); }
    /**
     * Gets the position of the separator which was added first.
     * Returns LISTBOX_ENTRY_NOTFOUND if there is no separator.
     */
    sal_Int32       GetSeparatorPos() const         { return maLBWindow->GetSeparatorPos(); }

    /**
     * Adds a new separator at the given position n.
     */
    void            AddSeparator( sal_Int32 n )     { maLBWindow->AddSeparator( n ); }

    void            SetTopEntry( sal_Int32  nTop )      { maLBWindow->SetTopEntry( nTop ); }
    sal_Int32       GetTopEntry() const             { return maLBWindow->GetTopEntry(); }
    void            ShowProminentEntry( sal_Int32  nPos ) { maLBWindow->ShowProminentEntry( nPos ); }
    using Window::IsVisible;
    bool            IsVisible( sal_Int32  nEntry ) const { return maLBWindow->IsVisible( nEntry ); }

    tools::Long            GetLeftIndent() const           { return maLBWindow->GetLeftIndent(); }
    void            SetLeftIndent( sal_uInt16 n )       { maLBWindow->SetLeftIndent( n ); }

    void            SetTravelSelect( bool bTravelSelect ) { maLBWindow->SetTravelSelect( bTravelSelect ); }
    bool            IsTravelSelect() const          { return maLBWindow->IsTravelSelect(); }
    bool            IsTrackingSelect() const            { return maLBWindow->IsTrackingSelect(); }

    void            EnableMultiSelection( bool bMulti ) { maLBWindow->EnableMultiSelection( bMulti ); }
    bool            IsMultiSelectionEnabled() const     { return maLBWindow->IsMultiSelectionEnabled(); }

    void            SetMultiSelectionSimpleMode( bool bSimple ) { maLBWindow->SetMultiSelectionSimpleMode( bSimple ); }

    void            SetReadOnly( bool b )           { maLBWindow->SetReadOnly( b ); }
    bool            IsReadOnly() const              { return maLBWindow->IsReadOnly(); }

    Size            CalcSize( sal_Int32  nMaxLines ) const              { return maLBWindow->CalcSize( nMaxLines ); }
    tools::Long            GetEntryHeight() const          { return maLBWindow->GetEntryHeight(); }
    tools::Long            GetEntryHeightWithMargin() const{ return maLBWindow->GetEntryHeightWithMargin(); }
    tools::Long            GetMaxEntryWidth() const        { return maLBWindow->GetMaxEntryWidth(); }

    void            SetScrollHdl( const Link<ImplListBox*,void>& rLink ) { maScrollHdl = rLink; }
    void            SetSelectHdl( const Link<LinkParamNone*,void>& rLink ) { maLBWindow->SetSelectHdl( rLink ); }
    void            SetCancelHdl( const Link<LinkParamNone*,void>& rLink ) { maLBWindow->SetCancelHdl( rLink ); }
    void            SetDoubleClickHdl( const Link<ImplListBoxWindow*,void>& rLink ) { maLBWindow->SetDoubleClickHdl( rLink ); }
    void            SetUserDrawHdl( const Link<UserDrawEvent*, void>& rLink ) { maLBWindow->SetUserDrawHdl( rLink ); }
    void            SetFocusHdl( const Link<sal_Int32,void>& rLink )  { maLBWindow->SetFocusHdl( rLink ); }
    void            SetListItemSelectHdl( const Link<LinkParamNone*,void>& rLink ) { maLBWindow->SetListItemSelectHdl( rLink ); }
    void            SetSelectionChangedHdl( const Link<sal_Int32,void>& rLnk ) { maLBWindow->GetEntryList().SetSelectionChangedHdl( rLnk ); }
    void            SetCallSelectionChangedHdl( bool bCall )    { maLBWindow->GetEntryList().SetCallSelectionChangedHdl( bCall ); }
    bool            IsSelectionChanged() const                  { return maLBWindow->IsSelectionChanged(); }
    sal_uInt16      GetSelectModifier() const                   { return maLBWindow->GetSelectModifier(); }
    void            SetHighlightColor(const Color& rColor);
    void            SetHighlightTextColor(const Color& rColor);

    void            SetMRUEntries( std::u16string_view rEntries, sal_Unicode cSep );
    OUString        GetMRUEntries( sal_Unicode cSep ) const;
    void            SetMaxMRUCount( sal_Int32  n )                  { maLBWindow->GetEntryList().SetMaxMRUCount( n ); }
    sal_Int32       GetMaxMRUCount() const                      { return maLBWindow->GetEntryList().GetMaxMRUCount(); }
    sal_uInt16      GetDisplayLineCount() const
    { return maLBWindow->GetDisplayLineCount(); }

    bool GetEdgeBlending() const { return mbEdgeBlending; }
    void SetEdgeBlending(bool bNew);
};

class ImplListBoxFloatingWindow final : public FloatingWindow
{
private:
    VclPtr<ImplListBox> mpImplLB;
    Size            maPrefSz;
    sal_uInt16      mnDDLineCount;
    sal_Int32       mnPopupModeStartSaveSelection;
    bool            mbAutoWidth;

    virtual bool    PreNotify( NotifyEvent& rNEvt ) override;

public:
                    ImplListBoxFloatingWindow( vcl::Window* pParent );
    virtual         ~ImplListBoxFloatingWindow() override;
    virtual void    dispose() override;
    void            SetImplListBox( ImplListBox* pLB )  { mpImplLB = pLB; }

    void            SetPrefSize( const Size& rSz )      { maPrefSz = rSz; }
    const Size&     GetPrefSize() const                 { return maPrefSz; }

    void            SetAutoWidth( bool b )              { mbAutoWidth = b; }

    tools::Rectangle GetParentRect() const;
    Size            CalcFloatSize(const tools::Rectangle& rParentRect) const;
    void            StartFloat( bool bStartTracking );

    virtual void    setPosSizePixel( tools::Long nX, tools::Long nY,
                                     tools::Long nWidth, tools::Long nHeight, PosSizeFlags nFlags = PosSizeFlags::All ) override;

    void            SetDropDownLineCount( sal_uInt16 n ) { mnDDLineCount = n; }
    sal_uInt16      GetDropDownLineCount() const { return mnDDLineCount; }

    sal_Int32       GetPopupModeStartSaveSelection() const { return mnPopupModeStartSaveSelection; }

    virtual void    Resize() override;
};

class ImplWin final : public Control
{
private:

    sal_Int32       mnItemPos;  ///< because of UserDraw I have to know which item I draw
    OUString        maString;
    Image           maImage;

    tools::Rectangle       maFocusRect;

    Link<void*,void> maMBDownHdl;

    bool            mbEdgeBlending : 1;

    void ImplDraw(vcl::RenderContext& rRenderContext, bool bLayout = false);
    virtual void  FillLayoutData() const override;

public:
                    ImplWin( vcl::Window* pParent, WinBits nWinStyle );

    virtual void    MouseButtonDown( const MouseEvent& rMEvt ) override;
    virtual void    Paint( vcl::RenderContext& rRenderContext, const tools::Rectangle& rRect ) override;
    virtual void    Resize() override;
    virtual void    GetFocus() override;
    virtual void    LoseFocus() override;

    sal_Int32       GetItemPos() const { return mnItemPos; }
    void            SetItemPos( sal_Int32  n ) { mnItemPos = n; }

    void            SetString( const OUString& rStr ) { maString = rStr; }

    void            SetImage( const Image& rImg ) { maImage = rImg; }

    void            SetMBDownHdl( const Link<void*,void>& rLink ) { maMBDownHdl = rLink; }

    void DrawEntry(vcl::RenderContext& rRenderContext, bool bLayout);

    bool GetEdgeBlending() const { return mbEdgeBlending; }
    void SetEdgeBlending(bool bNew) { mbEdgeBlending = bNew; }

    virtual void    ShowFocus(const tools::Rectangle& rRect) override;

    using Control::ImplInitSettings;
    virtual void ApplySettings(vcl::RenderContext& rRenderContext) override;

};

class ImplBtn final : public PushButton
{
private:
    Link<void*,void> maMBDownHdl;

public:
                    ImplBtn( vcl::Window* pParent, WinBits nWinStyle );

    virtual void    MouseButtonDown( const MouseEvent& rMEvt ) override;
    void            SetMBDownHdl( const Link<void*,void>& rLink ) { maMBDownHdl = rLink; }
};

void ImplInitDropDownButton( PushButton* pButton );

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
