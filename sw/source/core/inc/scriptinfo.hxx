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

#ifndef INCLUDED_SW_SOURCE_CORE_INC_SCRIPTINFO_HXX
#define INCLUDED_SW_SOURCE_CORE_INC_SCRIPTINFO_HXX

#include <vector>
#include <deque>
#include <unordered_set>
#include <rtl/ustrbuf.hxx>
#include <o3tl/typed_flags_set.hxx>
#include <i18nlangtag/lang.h>
#include <tools/long.hxx>
#include "TextFrameIndex.hxx"
#include <doc.hxx>

class SwTextNode;
class SwTextFrame;
class Point;
class MultiSelection;
enum class SwFontScript;
namespace sw { struct MergedPara; }
namespace sw::mark { class Bookmark; }

#define SPACING_PRECISION_FACTOR 100

// encapsulates information about script changes
class SwScriptInfo
{
public:
    enum CompType { KANA, SPECIAL_LEFT, SPECIAL_RIGHT, NONE, SPECIAL_MIDDLE};
    enum class MarkKind { Start = (1<<0), End = (1<<1), Point = (1<<2) };

private:
    //! Records a single change in script type.
    struct ScriptChangeInfo
    {
        TextFrameIndex position; //!< Character position at which we change script
        sal_uInt8       type;     //!< Script type (Latin/Asian/Complex) that we change to.
        ScriptChangeInfo(TextFrameIndex pos, sal_uInt8 typ) : position(pos), type(typ) {};
    };
    //TODO - This is sorted, so should probably be a std::set rather than vector.
    //       But we also use random access (probably unnecessarily).
    std::vector<ScriptChangeInfo> m_ScriptChanges;
    //! Records a single change in direction.
    struct DirectionChangeInfo
    {
        TextFrameIndex position; //!< Character position at which we change direction.
        sal_uInt8       type;     //!< Direction that we change to.
        DirectionChangeInfo(TextFrameIndex pos, sal_uInt8 typ) : position(pos), type(typ) {};
    };
    std::vector<DirectionChangeInfo> m_DirectionChanges;
    std::vector<TextFrameIndex> m_Kashida;
    std::vector<TextFrameIndex> m_HiddenChg;
    std::vector<std::tuple<TextFrameIndex, MarkKind, Color, SwMarkName, OUString>> m_Bookmarks;
    //! Records a single change in compression.
    struct CompressionChangeInfo
    {
        TextFrameIndex position; //!< Character position where the change occurs.
        TextFrameIndex length;   //!< Length of the segment.
        CompType  type;     //!< Type of compression that we change to.
        CompressionChangeInfo(TextFrameIndex pos, TextFrameIndex len, CompType typ) : position(pos), length(len), type(typ) {};
    };
    std::vector<CompressionChangeInfo> m_CompressionChanges;
#ifdef DBG_UTIL
    CompType DbgCompType(const TextFrameIndex nPos) const;
#endif

    TextFrameIndex m_nInvalidityPos;
    sal_uInt8 m_nDefaultDir;
    bool m_bAdjustBlock = false;
    bool m_bParagraphContainsKashidaScript = false;

    // examines the range [ nStart, nStart + nEnd ] if there are kanas
    // returns start index of kana entry in array, otherwise SAL_MAX_SIZE
    size_t HasKana(TextFrameIndex nStart, TextFrameIndex nEnd) const;

public:

    SwScriptInfo();
    ~SwScriptInfo();

    // partial init: only m_HiddenChg/m_Bookmarks
    void InitScriptInfoHidden(const SwTextNode& rNode, sw::MergedPara const* pMerged);
    // determines script changes
    void InitScriptInfo(const SwTextNode& rNode, sw::MergedPara const* pMerged, bool bRTL);
    void InitScriptInfo(const SwTextNode& rNode, sw::MergedPara const* pMerged);

    // set/get position from which data is invalid
    void SetInvalidityA(const TextFrameIndex nPos)
    {
        if (nPos < m_nInvalidityPos)
            m_nInvalidityPos = nPos;
    }
    TextFrameIndex GetInvalidityA() const
    {
        return m_nInvalidityPos;
    }

    // get default direction for paragraph
    sal_uInt8 GetDefaultDir() const { return m_nDefaultDir; };

    // array operations, nCnt refers to array position
    size_t CountScriptChg() const { return m_ScriptChanges.size(); }
    TextFrameIndex GetScriptChg(const size_t nCnt) const
    {
        assert(nCnt < m_ScriptChanges.size());
        return m_ScriptChanges[nCnt].position;
    }
    sal_uInt8 GetScriptType( const size_t nCnt ) const
    {
        assert( nCnt < m_ScriptChanges.size());
        return m_ScriptChanges[nCnt].type;
    }

    size_t CountDirChg() const { return m_DirectionChanges.size(); }
    TextFrameIndex GetDirChg(const size_t nCnt) const
    {
        assert(nCnt < m_DirectionChanges.size());
        return m_DirectionChanges[ nCnt ].position;
    }
    sal_uInt8 GetDirType( const size_t nCnt ) const
    {
        assert(nCnt < m_DirectionChanges.size());
        return m_DirectionChanges[ nCnt ].type;
    }

    bool ParagraphIsJustified() const { return m_bAdjustBlock; }
    bool ParagraphContainsKashidaScript() const { return m_bParagraphContainsKashidaScript; }

    size_t CountCompChg() const { return m_CompressionChanges.size(); };
    TextFrameIndex GetCompStart(const size_t nCnt) const
    {
        assert(nCnt < m_CompressionChanges.size());
        return m_CompressionChanges[ nCnt ].position;
    }
    TextFrameIndex GetCompLen(const size_t nCnt) const
    {
        assert(nCnt < m_CompressionChanges.size());
        return m_CompressionChanges[ nCnt ].length;
    }
    CompType GetCompType( const size_t nCnt ) const
    {
        assert(nCnt < m_CompressionChanges.size());
        return m_CompressionChanges[ nCnt ].type;
    }

    size_t CountHiddenChg() const { return m_HiddenChg.size(); };
    TextFrameIndex GetHiddenChg(const size_t nCnt) const
    {
        assert(nCnt < m_HiddenChg.size());
        return m_HiddenChg[ nCnt ];
    }
    TextFrameIndex NextHiddenChg(TextFrameIndex nPos) const;
    TextFrameIndex NextBookmark(TextFrameIndex nPos) const;
    std::vector<std::tuple<MarkKind, Color, SwMarkName, OUString>>
            GetBookmarks(TextFrameIndex const nPos);
    static void CalcHiddenRanges(const SwTextNode& rNode,
            MultiSelection& rHiddenMulti,
            std::vector<std::pair<sw::mark::Bookmark*, MarkKind>> * pBookmarks);
    static void selectHiddenTextProperty(const SwTextNode& rNode,
            MultiSelection &rHiddenMulti,
            std::vector<std::pair<sw::mark::Bookmark*, MarkKind>> * pBookmarks);
    static void selectRedLineDeleted(const SwTextNode& rNode, MultiSelection &rHiddenMulti, bool bSelect=true);

    // "high" level operations, nPos refers to string position
    TextFrameIndex NextScriptChg(TextFrameIndex nPos) const;
    sal_Int16 ScriptType(const TextFrameIndex nPos) const;

    // Returns the position of the next direction level change.
    // If bLevel is set, the position of the next level which is smaller
    // than the level at position nPos is returned. This is required to
    // obtain the end of a SwBidiPortion
    TextFrameIndex NextDirChg(const TextFrameIndex nPos,
                           const sal_uInt8* pLevel = nullptr) const;
    sal_uInt8 DirType(const TextFrameIndex nPos) const;

    // HIDDEN TEXT STUFF START

/** Hidden text range information - static and non-version

    @descr  Determines if a given position is inside a hidden text range. The
            static version tries to obtain a valid SwScriptInfo object
            via the SwTextNode, otherwise it calculates the values from scratch.
            The non-static version uses the internally cached information
            for the calculation.

    @param  rNode
                The text node.
    @param  nPos
                The given position that should be checked.
    @param  rnStartPos
                Return parameter for the start position of the hidden range.
                COMPLETE_STRING if nPos is not inside a hidden range.
    @param  rnEndPos
                Return parameter for the end position of the hidden range.
                0 if nPos is not inside a hidden range.
    @param  rnEndPos
                Return parameter that contains all the hidden text ranges. Optional.
    @return
            returns true if there are any hidden characters in this paragraph.

*/
    static bool GetBoundsOfHiddenRange( const SwTextNode& rNode, sal_Int32 nPos,
                                        sal_Int32& rnStartPos, sal_Int32& rnEndPos,
                                        std::vector<sal_Int32>* pList = nullptr );
    bool GetBoundsOfHiddenRange(TextFrameIndex nPos, TextFrameIndex & rnStartPos,
                                TextFrameIndex & rnEndPos) const;

    static bool IsInHiddenRange( const SwTextNode& rNode, sal_Int32 nPos );

/** Hidden text attribute handling

    @descr  Takes a string and either deletes the hidden ranges or sets
            a given character in place of the hidden characters.

    @param  rNode
                The text node.
    @param  rText
                The string to modify.
    @param  cChar
                The character that should replace the hidden characters.
    @param  bDel
                If set, the hidden ranges will be deleted from the text node.
 */
    static sal_Int32 MaskHiddenRanges(
            const SwTextNode& rNode, OUStringBuffer& rText,
                                    const sal_Int32 nStt, const sal_Int32 nEnd,
                                    const sal_Unicode cChar );

/** Hidden text attribute handling

    @descr  Takes a SwTextNode and deletes the hidden ranges from the node.

    @param  rNode
                The text node.
 */
    static void DeleteHiddenRanges( SwTextNode& rNode );

    // HIDDEN TEXT STUFF END

    // modifies the kerning array according to a given compress value
    tools::Long Compress( KernArray& rKernArray, TextFrameIndex nIdx, TextFrameIndex nLen,
                   const sal_uInt16 nCompress, const sal_uInt16 nFontHeight,
                   const bool bCentered,
                   Point* pPoint = nullptr ) const;

/** retrieves kashida opportunities for the paragraph.
*/
    std::span<TextFrameIndex const> GetKashidaPositions() const { return m_Kashida; }

/** returns the count of kashida opportunities for a substring.
*/
    tools::Long CountKashidaPositions(TextFrameIndex nIdx, TextFrameIndex nEnd) const;

/** replaces kashida opportunities for the paragraph

   aKashidaPositions: buffer containing char indices of the
                      kashida opportunities relative to the paragraph
*/
    void ReplaceKashidaPositions(std::vector<TextFrameIndex> aKashidaPositions);

/** Checks if text is in a script that allows kashida justification.

     @descr  Checks if text is in a language that allows kashida justification.
     @param  rText
                 The text to check
     @param  nStt
                 Start index of the text
     @return Returns true if the script is Arabic or Syriac
 */
    static bool IsKashidaScriptText(const OUString& rText, TextFrameIndex nStt,
                                    TextFrameIndex nLen);

/** Performs a thai justification on the kerning array

    @descr  Add some extra space for thai justification to the
            positions in the kerning array.
    @param  aText
                The String
    @param  pKernArray
                The printers kerning array. Optional.
    @param  nIdx
                Start referring to the paragraph.
    @param  nLen
                The number of characters to be considered.
    @param  nSpaceAdd
                The value which has to be added to the cells.
    @return The number of extra spaces in the given range
*/
    static TextFrameIndex ThaiJustify( std::u16string_view aText, KernArray* pKernArray,
                                  TextFrameIndex nIdx,
                                  TextFrameIndex nLen,
                                  TextFrameIndex nNumberOfBlanks = TextFrameIndex(0),
                                  tools::Long nSpaceAdd = 0 );

    static TextFrameIndex CountCJKCharacters(const OUString &rText,
            TextFrameIndex nPos, TextFrameIndex nEnd, LanguageType aLang);

    static void CJKJustify( const OUString& rText, KernArray& rKernArray,
                                  TextFrameIndex nStt,
                                  TextFrameIndex nLen, LanguageType aLang,
                                  tools::Long nSpaceAdd, bool bIsSpaceStop );

    /// return a frame for the node, ScriptInfo is its member...
    /// (many clients need both frame and SI, and both have to match)
    static SwScriptInfo* GetScriptInfo( const SwTextNode& rNode,
                                        SwTextFrame const** o_pFrame = nullptr,
                                        bool bAllowInvalid = false);

    SwFontScript WhichFont(TextFrameIndex nIdx) const;
    static SwFontScript WhichFont(sal_Int32 nIdx, OUString const & rText);
};

namespace o3tl
{

template<> struct typed_flags<SwScriptInfo::MarkKind> : is_typed_flags<SwScriptInfo::MarkKind, 0x07> {};

}

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
