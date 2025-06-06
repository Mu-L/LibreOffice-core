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

#include <externalrefmgr.hxx>
#include <document.hxx>
#include <token.hxx>
#include <tokenarray.hxx>
#include <address.hxx>
#include <tablink.hxx>
#include <docsh.hxx>
#include <scextopt.hxx>
#include <rangenam.hxx>
#include <formulacell.hxx>
#include <utility>
#include <viewdata.hxx>
#include <tabvwsh.hxx>
#include <sc.hrc>
#include <globstr.hrc>
#include <scresid.hxx>
#include <cellvalue.hxx>
#include <defaultsoptions.hxx>
#include <scmod.hxx>

#include <o3tl/safeint.hxx>
#include <osl/diagnose.h>
#include <osl/file.hxx>
#include <sfx2/app.hxx>
#include <sfx2/docfile.hxx>
#include <sfx2/event.hxx>
#include <sfx2/fcontnr.hxx>
#include <sfx2/objsh.hxx>
#include <svl/itemset.hxx>
#include <svl/numformat.hxx>
#include <svl/stritem.hxx>
#include <svl/urihelper.hxx>
#include <svl/sharedstringpool.hxx>
#include <sfx2/linkmgr.hxx>
#include <tools/hostfilter.hxx>
#include <tools/urlobj.hxx>
#include <unotools/charclass.hxx>
#include <comphelper/configuration.hxx>
#include <unotools/ucbhelper.hxx>
#include <vcl/svapp.hxx>
#include <vcl/weld.hxx>
#include <stringutil.hxx>
#include <scmatrix.hxx>
#include <columnspanset.hxx>
#include <column.hxx>
#include <com/sun/star/document/MacroExecMode.hpp>
#include <com/sun/star/document/UpdateDocMode.hpp>
#include <sal/log.hxx>

#include <memory>
#include <algorithm>

using ::std::unique_ptr;
using ::com::sun::star::uno::Any;
using ::std::vector;
using ::std::find_if;
using ::std::for_each;
using ::std::distance;
using ::std::pair;
using namespace formula;

#define SRCDOC_LIFE_SPAN     30000      // 5 minutes (in 100th of a sec)
#define SRCDOC_SCAN_INTERVAL 1000*30    // every 30 seconds (in msec)

namespace {

class TabNameSearchPredicate
{
public:
    explicit TabNameSearchPredicate(const OUString& rSearchName) :
        maSearchName(ScGlobal::getCharClass().uppercase(rSearchName))
    {
    }

    bool operator()(const ScExternalRefCache::TableName& rTabNameSet) const
    {
        // Ok, I'm doing case insensitive search here.
        return rTabNameSet.maUpperName == maSearchName;
    }

private:
    OUString maSearchName;
};

class FindSrcFileByName
{
public:
    explicit FindSrcFileByName(const OUString& rMatchName) :
        mrMatchName(rMatchName)
    {
    }

    bool operator()(const ScExternalRefManager::SrcFileData& rSrcData) const
    {
        return rSrcData.maFileName == mrMatchName;
    }

private:
    const OUString& mrMatchName;
};

class NotifyLinkListener
{
public:
    NotifyLinkListener(sal_uInt16 nFileId, ScExternalRefManager::LinkUpdateType eType) :
        mnFileId(nFileId), meType(eType) {}

    void operator() (ScExternalRefManager::LinkListener* p) const
    {
        p->notify(mnFileId, meType);
    }
private:
    sal_uInt16 mnFileId;
    ScExternalRefManager::LinkUpdateType meType;
};

struct UpdateFormulaCell
{
    void operator() (ScFormulaCell* pCell) const
    {
        // Check to make sure the cell really contains svExternal*.
        // External names, external cell and range references all have a
        // token of svExternal*. Additionally check for INDIRECT() that can be
        // called with any constructed URI string.
        ScTokenArray* pCode = pCell->GetCode();
        if (!pCode->HasExternalRef() && !pCode->HasOpCode(ocIndirect))
            return;

        if (pCode->GetCodeError() != FormulaError::NONE)
        {
            // Clear the error code, or a cell with error won't get re-compiled.
            pCode->SetCodeError(FormulaError::NONE);
            pCell->SetCompile(true);
            pCell->CompileTokenArray();
        }

        pCell->SetDirty();
    }
};

class RemoveFormulaCell
{
public:
    explicit RemoveFormulaCell(ScFormulaCell* p) : mpCell(p) {}
    void operator() (pair<const sal_uInt16, ScExternalRefManager::RefCellSet>& r) const
    {
        r.second.erase(mpCell);
    }
private:
    ScFormulaCell* mpCell;
};

class ConvertFormulaToStatic
{
public:
    explicit ConvertFormulaToStatic(ScDocument* pDoc) : mpDoc(pDoc) {}
    void operator() (ScFormulaCell* pCell) const
    {
        ScAddress aPos = pCell->aPos;

        // We don't check for empty cells because empty external cells are
        // treated as having a value of 0.

        if (pCell->IsValue())
        {
            // Turn this into value cell.
            mpDoc->SetValue(aPos, pCell->GetValue());
        }
        else
        {
            // string cell otherwise.
            ScSetStringParam aParam;
            aParam.setTextInput();
            mpDoc->SetString(aPos, pCell->GetString().getString(), &aParam);
        }
    }
private:
    ScDocument* mpDoc;
};

/**
 * Check whether a named range contains an external reference to a
 * particular document.
 */
bool hasRefsToSrcDoc(ScRangeData& rData, sal_uInt16 nFileId)
{
    ScTokenArray* pArray = rData.GetCode();
    if (!pArray)
        return false;

    formula::FormulaTokenArrayPlainIterator aIter(*pArray);
    formula::FormulaToken* p = aIter.GetNextReference();
    for (; p; p = aIter.GetNextReference())
    {
        if (!p->IsExternalRef())
            continue;

        if (p->GetIndex() == nFileId)
            return true;
    }
    return false;
}

class EraseRangeByIterator
{
    ScRangeName& mrRanges;
public:
    explicit EraseRangeByIterator(ScRangeName& rRanges) : mrRanges(rRanges) {}
    void operator() (const ScRangeName::const_iterator& itr)
    {
        mrRanges.erase(itr);
    }
};

/**
 * Remove all named ranges that contain references to specified source
 * document.
 */
void removeRangeNamesBySrcDoc(ScRangeName& rRanges, sal_uInt16 nFileId)
{
    ScRangeName::const_iterator itr = rRanges.begin(), itrEnd = rRanges.end();
    vector<ScRangeName::const_iterator> v;
    for (; itr != itrEnd; ++itr)
    {
        if (hasRefsToSrcDoc(*itr->second, nFileId))
            v.push_back(itr);
    }
    for_each(v.begin(), v.end(), EraseRangeByIterator(rRanges));
}

}

ScExternalRefCache::Table::Table()
    : mbReferenced( true )
      // Prevent accidental data loss due to lack of knowledge.
{
}

ScExternalRefCache::Table::~Table()
{
}

void ScExternalRefCache::Table::clear()
{
    maRows.clear();
    maCachedRanges.RemoveAll();
    mbReferenced = true;
}

void ScExternalRefCache::Table::setReferenced( bool bReferenced )
{
    mbReferenced = bReferenced;
}

bool ScExternalRefCache::Table::isReferenced() const
{
    return mbReferenced;
}

void ScExternalRefCache::Table::setCell(SCCOL nCol, SCROW nRow, TokenRef const & pToken, sal_uLong nFmtIndex, bool bSetCacheRange)
{
    using ::std::pair;
    RowsDataType::iterator itrRow = maRows.find(nRow);
    if (itrRow == maRows.end())
    {
        // This row does not exist yet.
        pair<RowsDataType::iterator, bool> res = maRows.emplace(
            nRow, RowDataType());

        if (!res.second)
            return;

        itrRow = res.first;
    }

    // Insert this token into the specified column location.  I don't need to
    // check for existing data.  Just overwrite it.
    RowDataType& rRow = itrRow->second;
    ScExternalRefCache::Cell aCell;
    aCell.mxToken = pToken;
    aCell.mnFmtIndex = nFmtIndex;
    rRow.emplace(nCol, aCell);
    if (bSetCacheRange)
        setCachedCell(nCol, nRow);
}

ScExternalRefCache::TokenRef ScExternalRefCache::Table::getCell(SCCOL nCol, SCROW nRow, sal_uInt32* pnFmtIndex) const
{
    RowsDataType::const_iterator itrTable = maRows.find(nRow);
    if (itrTable == maRows.end())
    {
        // this table doesn't have the specified row.
        return getEmptyOrNullToken(nCol, nRow);
    }

    const RowDataType& rRowData = itrTable->second;
    RowDataType::const_iterator itrRow = rRowData.find(nCol);
    if (itrRow == rRowData.end())
    {
        // this row doesn't have the specified column.
        return getEmptyOrNullToken(nCol, nRow);
    }

    const Cell& rCell = itrRow->second;
    if (pnFmtIndex)
        *pnFmtIndex = rCell.mnFmtIndex;

    return rCell.mxToken;
}

bool ScExternalRefCache::Table::hasRow( SCROW nRow ) const
{
    RowsDataType::const_iterator itrRow = maRows.find(nRow);
    return itrRow != maRows.end();
}

template< typename P >
void ScExternalRefCache::Table::getAllRows(vector<SCROW>& rRows, P predicate) const
{
    vector<SCROW> aRows;
    aRows.reserve(maRows.size());
    for (const auto& rEntry : maRows)
        if (predicate(rEntry))
            aRows.push_back(rEntry.first);

    // hash map is not ordered, so we need to explicitly sort it.
    ::std::sort(aRows.begin(), aRows.end());
    rRows.swap(aRows);
}

void ScExternalRefCache::Table::getAllRows(vector<SCROW>& rRows, SCROW nLow, SCROW nHigh) const
{
    getAllRows(rRows,
        [nLow, nHigh](std::pair<SCROW, RowDataType> rEntry) { return (nLow <= rEntry.first && rEntry.first <= nHigh); });
}

void ScExternalRefCache::Table::getAllRows(vector<SCROW>& rRows) const
{
    getAllRows(rRows, [](std::pair<SCROW, RowDataType>) { return true; } );
}

::std::pair< SCROW, SCROW > ScExternalRefCache::Table::getRowRange() const
{
    ::std::pair< SCROW, SCROW > aRange( 0, 0 );
    if( !maRows.empty() )
    {
        // iterate over entire container (hash map is not sorted by key)
        auto itMinMax = std::minmax_element(maRows.begin(), maRows.end(),
            [](const RowsDataType::value_type& a, const RowsDataType::value_type& b) { return a.first < b.first; });
        aRange.first = itMinMax.first->first;
        aRange.second = itMinMax.second->first + 1;
    }
    return aRange;
}

template< typename P >
void ScExternalRefCache::Table::getAllCols(SCROW nRow, vector<SCCOL>& rCols, P predicate) const
{
    RowsDataType::const_iterator itrRow = maRows.find(nRow);
    if (itrRow == maRows.end())
        // this table doesn't have the specified row.
        return;

    const RowDataType& rRowData = itrRow->second;
    vector<SCCOL> aCols;
    aCols.reserve(rRowData.size());
    for (const auto& rCol : rRowData)
        if (predicate(rCol))
            aCols.push_back(rCol.first);

    // hash map is not ordered, so we need to explicitly sort it.
    ::std::sort(aCols.begin(), aCols.end());
    rCols.swap(aCols);
}

void ScExternalRefCache::Table::getAllCols(SCROW nRow, vector<SCCOL>& rCols, SCCOL nLow, SCCOL nHigh) const
{
    getAllCols(nRow, rCols,
        [nLow, nHigh](std::pair<SCCOL, Cell> rCol) { return nLow <= rCol.first && rCol.first <= nHigh; } );
}

void ScExternalRefCache::Table::getAllCols(SCROW nRow, vector<SCCOL>& rCols) const
{
    getAllCols(nRow, rCols, [](std::pair<SCCOL, Cell>) { return true; } );
}

::std::pair< SCCOL, SCCOL > ScExternalRefCache::Table::getColRange( SCROW nRow ) const
{
    ::std::pair< SCCOL, SCCOL > aRange( 0, 0 );

    RowsDataType::const_iterator itrRow = maRows.find( nRow );
    if (itrRow == maRows.end())
        // this table doesn't have the specified row.
        return aRange;

    const RowDataType& rRowData = itrRow->second;
    if( !rRowData.empty() )
    {
        // iterate over entire container (hash map is not sorted by key)
        auto itMinMax = std::minmax_element(rRowData.begin(), rRowData.end(),
            [](const RowDataType::value_type& a, const RowDataType::value_type& b) { return a.first < b.first; });
        aRange.first = itMinMax.first->first;
        aRange.second = itMinMax.second->first + 1;
    }
    return aRange;
}

void ScExternalRefCache::Table::getAllNumberFormats(vector<sal_uInt32>& rNumFmts) const
{
    for (const auto& rRow : maRows)
    {
        const RowDataType& rRowData = rRow.second;
        for (const auto& rCol : rRowData)
        {
            const Cell& rCell = rCol.second;
            rNumFmts.push_back(rCell.mnFmtIndex);
        }
    }
}

bool ScExternalRefCache::Table::isRangeCached(SCCOL nCol1, SCROW nRow1, SCCOL nCol2, SCROW nRow2) const
{
    return maCachedRanges.Contains(ScRange(nCol1, nRow1, 0, nCol2, nRow2, 0));
}

void ScExternalRefCache::Table::setCachedCell(SCCOL nCol, SCROW nRow)
{
    setCachedCellRange(nCol, nRow, nCol, nRow);
}

void ScExternalRefCache::Table::setCachedCellRange(SCCOL nCol1, SCROW nRow1, SCCOL nCol2, SCROW nRow2)
{
    ScRange aRange(nCol1, nRow1, 0, nCol2, nRow2, 0);
    maCachedRanges.Join(aRange);
}

void ScExternalRefCache::Table::setWholeTableCached()
{
    setCachedCellRange(0, 0, MAXCOL, MAXROW);
}

bool ScExternalRefCache::Table::isInCachedRanges(SCCOL nCol, SCROW nRow) const
{
    return maCachedRanges.Contains(ScRange(nCol, nRow, 0, nCol, nRow, 0));
}

ScExternalRefCache::TokenRef ScExternalRefCache::Table::getEmptyOrNullToken(
    SCCOL nCol, SCROW nRow) const
{
    if (isInCachedRanges(nCol, nRow))
    {
        TokenRef p(new ScEmptyCellToken(false, false));
        return p;
    }
    return TokenRef();
}

ScExternalRefCache::TableName::TableName(OUString aUpper, OUString aReal) :
    maUpperName(std::move(aUpper)), maRealName(std::move(aReal))
{
}

ScExternalRefCache::CellFormat::CellFormat() :
    mbIsSet(false), mnType(SvNumFormatType::ALL), mnIndex(0)
{
}

ScExternalRefCache::ScExternalRefCache(const ScDocument& rDoc)
    : mrDoc(rDoc)
{
}

ScExternalRefCache::~ScExternalRefCache() {}

const OUString* ScExternalRefCache::getRealTableName(sal_uInt16 nFileId, const OUString& rTabName) const
{
    std::unique_lock aGuard(maMtxDocs);

    DocDataType::const_iterator itrDoc = maDocs.find(nFileId);
    if (itrDoc == maDocs.end())
    {
        // specified document is not cached.
        return nullptr;
    }

    const DocItem& rDoc = itrDoc->second;
    TableNameIndexMap::const_iterator itrTabId = rDoc.findTableNameIndex( rTabName);
    if (itrTabId == rDoc.maTableNameIndex.end())
    {
        // the specified table is not in cache.
        return nullptr;
    }

    return &rDoc.maTableNames[itrTabId->second].maRealName;
}

const OUString* ScExternalRefCache::getRealRangeName(sal_uInt16 nFileId, const OUString& rRangeName) const
{
    std::unique_lock aGuard(maMtxDocs);

    DocDataType::const_iterator itrDoc = maDocs.find(nFileId);
    if (itrDoc == maDocs.end())
    {
        // specified document is not cached.
        return nullptr;
    }

    const DocItem& rDoc = itrDoc->second;
    NamePairMap::const_iterator itr = rDoc.maRealRangeNameMap.find(
        ScGlobal::getCharClass().uppercase(rRangeName));
    if (itr == rDoc.maRealRangeNameMap.end())
        // range name not found.
        return nullptr;

    return &itr->second;
}

ScExternalRefCache::TokenRef ScExternalRefCache::getCellData(
    sal_uInt16 nFileId, const OUString& rTabName, SCCOL nCol, SCROW nRow, sal_uInt32* pnFmtIndex)
{
    std::unique_lock aGuard(maMtxDocs);

    DocDataType::const_iterator itrDoc = maDocs.find(nFileId);
    if (itrDoc == maDocs.end())
    {
        // specified document is not cached.
        return TokenRef();
    }

    const DocItem& rDoc = itrDoc->second;
    TableNameIndexMap::const_iterator itrTabId = rDoc.findTableNameIndex( rTabName);
    if (itrTabId == rDoc.maTableNameIndex.end())
    {
        // the specified table is not in cache.
        return TokenRef();
    }

    const TableTypeRef& pTableData = rDoc.maTables[itrTabId->second];
    if (!pTableData)
    {
        // the table data is not instantiated yet.
        return TokenRef();
    }

    return pTableData->getCell(nCol, nRow, pnFmtIndex);
}

ScExternalRefCache::TokenArrayRef ScExternalRefCache::getCellRangeData(
    sal_uInt16 nFileId, const OUString& rTabName, const ScRange& rRange)
{
    std::unique_lock aGuard(maMtxDocs);

    DocDataType::iterator itrDoc = maDocs.find(nFileId);
    if (itrDoc == maDocs.end())
        // specified document is not cached.
        return TokenArrayRef();

    DocItem& rDoc = itrDoc->second;

    TableNameIndexMap::const_iterator itrTabId = rDoc.findTableNameIndex( rTabName);
    if (itrTabId == rDoc.maTableNameIndex.end())
        // the specified table is not in cache.
        return TokenArrayRef();

    const ScAddress& s = rRange.aStart;
    const ScAddress& e = rRange.aEnd;

    const SCTAB nTab1 = s.Tab(), nTab2 = e.Tab();
    const SCCOL nCol1 = s.Col(), nCol2 = e.Col();
    const SCROW nRow1 = s.Row(), nRow2 = e.Row();

    // Make sure I have all the tables cached.
    size_t nTabFirstId = itrTabId->second;
    size_t nTabLastId  = nTabFirstId + nTab2 - nTab1;
    if (nTabLastId >= rDoc.maTables.size())
        // not all tables are cached.
        return TokenArrayRef();

    ScRange aCacheRange( nCol1, nRow1, static_cast<SCTAB>(nTabFirstId), nCol2, nRow2, static_cast<SCTAB>(nTabLastId));

    RangeArrayMap::const_iterator itrRange = rDoc.maRangeArrays.find( aCacheRange);
    if (itrRange != rDoc.maRangeArrays.end())
        // Cache hit!
        return itrRange->second;

    std::unique_ptr<ScRange> pNewRange;
    TokenArrayRef pArray;
    bool bFirstTab = true;
    for (size_t nTab = nTabFirstId; nTab <= nTabLastId; ++nTab)
    {
        TableTypeRef pTab = rDoc.maTables[nTab];
        if (!pTab)
            return TokenArrayRef();

        SCCOL nDataCol1 = nCol1, nDataCol2 = nCol2;
        SCROW nDataRow1 = nRow1, nDataRow2 = nRow2;

        if (!pTab->isRangeCached(nDataCol1, nDataRow1, nDataCol2, nDataRow2))
        {
            // specified range is not entirely within cached ranges.
            return TokenArrayRef();
        }

        SCSIZE nMatrixColumns = static_cast<SCSIZE>(nDataCol2-nDataCol1+1);
        SCSIZE nMatrixRows = static_cast<SCSIZE>(nDataRow2-nDataRow1+1);
        ScMatrixRef xMat = new ScMatrix( nMatrixColumns, nMatrixRows);

        // Needed in shrink and fill.
        vector<SCROW> aRows;
        pTab->getAllRows(aRows, nDataRow1, nDataRow2);
        bool bFill = true;

        // Check if size could be allocated and if not skip the fill, there's
        // one error element instead. But retry first with the actual data area
        // if that is smaller than the original range, which works for most
        // functions just not some that operate/compare with the original size
        // and expect empty values in non-data areas.
        // Restrict this though to ranges of entire columns or rows, other
        // ranges might be on purpose. (Other special cases to handle?)
        /* TODO: sparse matrix could help */
        SCSIZE nMatCols, nMatRows;
        xMat->GetDimensions( nMatCols, nMatRows);
        if (nMatCols != nMatrixColumns || nMatRows != nMatrixRows)
        {
            bFill = false;
            if (aRows.empty())
            {
                // There's no data at all. Set the one matrix element to empty
                // for column-repeated and row-repeated access.
                xMat->PutEmpty(0,0);
            }
            else if ((nCol1 == 0 && nCol2 == MAXCOL) || (nRow1 == 0 && nRow2 == MAXROW))
            {
                nDataRow1 = aRows.front();
                nDataRow2 = aRows.back();
                SCCOL nMinCol = std::numeric_limits<SCCOL>::max();
                SCCOL nMaxCol = std::numeric_limits<SCCOL>::min();
                for (const auto& rRow : aRows)
                {
                    vector<SCCOL> aCols;
                    pTab->getAllCols(rRow, aCols, nDataCol1, nDataCol2);
                    if (!aCols.empty())
                    {
                        nMinCol = std::min( nMinCol, aCols.front());
                        nMaxCol = std::max( nMaxCol, aCols.back());
                    }
                }

                if (nMinCol <= nMaxCol && ((o3tl::make_unsigned(nMaxCol-nMinCol+1) < nMatrixColumns) ||
                            (o3tl::make_unsigned(nDataRow2-nDataRow1+1) < nMatrixRows)))
                {
                    nMatrixColumns = static_cast<SCSIZE>(nMaxCol-nMinCol+1);
                    nMatrixRows = static_cast<SCSIZE>(nDataRow2-nDataRow1+1);
                    xMat = new ScMatrix( nMatrixColumns, nMatrixRows);
                    xMat->GetDimensions( nMatCols, nMatRows);
                    if (nMatCols == nMatrixColumns && nMatRows == nMatrixRows)
                    {
                        nDataCol1 = nMinCol;
                        nDataCol2 = nMaxCol;
                        bFill = true;
                    }
                }
            }
        }

        if (bFill)
        {
            // Only fill non-empty cells, for better performance.
            for (SCROW nRow : aRows)
            {
                vector<SCCOL> aCols;
                pTab->getAllCols(nRow, aCols, nDataCol1, nDataCol2);
                for (SCCOL nCol : aCols)
                {
                    TokenRef pToken = pTab->getCell(nCol, nRow);
                    if (!pToken)
                        // This should never happen!
                        return TokenArrayRef();

                    SCSIZE nC = nCol - nDataCol1, nR = nRow - nDataRow1;
                    switch (pToken->GetType())
                    {
                        case svDouble:
                            xMat->PutDouble(pToken->GetDouble(), nC, nR);
                            break;
                        case svString:
                            xMat->PutString(pToken->GetString(), nC, nR);
                            break;
                        default:
                            ;
                    }
                }
            }

            if (!bFirstTab)
                pArray->AddOpCode(ocSep);

            ScMatrixToken aToken(std::move(xMat));
            if (!pArray)
                pArray = std::make_shared<ScTokenArray>(mrDoc);
            pArray->AddToken(aToken);

            bFirstTab = false;

            if (!pNewRange)
                pNewRange.reset(new ScRange(nDataCol1, nDataRow1, nTab, nDataCol2, nDataRow2, nTab));
            else
                pNewRange->ExtendTo(ScRange(nDataCol1, nDataRow1, nTab, nDataCol2, nDataRow2, nTab));
        }
    }

    rDoc.maRangeArrays.emplace(aCacheRange, pArray);
    if (pNewRange && *pNewRange != aCacheRange)
        rDoc.maRangeArrays.emplace(*pNewRange, pArray);

    return pArray;
}

ScExternalRefCache::TokenArrayRef ScExternalRefCache::getRangeNameTokens(sal_uInt16 nFileId, const OUString& rName)
{
    std::unique_lock aGuard(maMtxDocs);

    DocItem* pDoc = getDocItem(aGuard, nFileId);
    if (!pDoc)
        return TokenArrayRef();

    RangeNameMap& rMap = pDoc->maRangeNames;
    RangeNameMap::const_iterator itr = rMap.find(
        ScGlobal::getCharClass().uppercase(rName));
    if (itr == rMap.end())
        return TokenArrayRef();

    return itr->second;
}

void ScExternalRefCache::setRangeNameTokens(sal_uInt16 nFileId, const OUString& rName, const TokenArrayRef& pArray)
{
    std::unique_lock aGuard(maMtxDocs);

    DocItem* pDoc = getDocItem(aGuard, nFileId);
    if (!pDoc)
        return;

    OUString aUpperName = ScGlobal::getCharClass().uppercase(rName);
    RangeNameMap& rMap = pDoc->maRangeNames;
    rMap.emplace(aUpperName, pArray);
    pDoc->maRealRangeNameMap.emplace(aUpperName, rName);
}

bool ScExternalRefCache::isValidRangeName(sal_uInt16 nFileId, const OUString& rName) const
{
    std::unique_lock aGuard(maMtxDocs);

    DocItem* pDoc = getDocItem(aGuard, nFileId);
    if (!pDoc)
        return false;

    OUString aUpperName = ScGlobal::getCharClass().uppercase(rName);
    const RangeNameMap& rMap = pDoc->maRangeNames;
    return rMap.count(aUpperName) > 0;
}

void ScExternalRefCache::setRangeName(sal_uInt16 nFileId, const OUString& rName)
{
    std::unique_lock aGuard(maMtxDocs);

    DocItem* pDoc = getDocItem(aGuard, nFileId);
    if (!pDoc)
        return;

    OUString aUpperName = ScGlobal::getCharClass().uppercase(rName);
    pDoc->maRealRangeNameMap.emplace(aUpperName, rName);
}

void ScExternalRefCache::setCellData(sal_uInt16 nFileId, const OUString& rTabName, SCCOL nCol, SCROW nRow,
                                     TokenRef const & pToken, sal_uLong nFmtIndex)
{
    if (!isDocInitialized(nFileId))
        return;

    using ::std::pair;
    DocItem* pDocItem = getDocItem(nFileId);
    if (!pDocItem)
        return;

    DocItem& rDoc = *pDocItem;

    // See if the table by this name already exists.
    TableNameIndexMap::const_iterator itrTabName = rDoc.findTableNameIndex( rTabName);
    if (itrTabName == rDoc.maTableNameIndex.end())
        // Table not found.  Maybe the table name or the file id is wrong ???
        return;

    TableTypeRef& pTableData = rDoc.maTables[itrTabName->second];
    if (!pTableData)
        pTableData = std::make_shared<Table>();

    pTableData->setCell(nCol, nRow, pToken, nFmtIndex);
    pTableData->setCachedCell(nCol, nRow);
}

void ScExternalRefCache::setCellRangeData(sal_uInt16 nFileId, const ScRange& rRange, const vector<SingleRangeData>& rData,
                                          const TokenArrayRef& pArray)
{
    using ::std::pair;
    if (rData.empty() || !isDocInitialized(nFileId))
        // nothing to cache
        return;

    // First, get the document item for the given file ID.
    DocItem* pDocItem = getDocItem(nFileId);
    if (!pDocItem)
        return;

    DocItem& rDoc = *pDocItem;

    // Now, find the table position of the first table to cache.
    const OUString& rFirstTabName = rData.front().maTableName;
    TableNameIndexMap::const_iterator itrTabName = rDoc.findTableNameIndex( rFirstTabName);
    if (itrTabName == rDoc.maTableNameIndex.end())
    {
        // table index not found.
        return;
    }

    size_t nTabFirstId = itrTabName->second;
    SCROW nRow1 = rRange.aStart.Row(), nRow2 = rRange.aEnd.Row();
    SCCOL nCol1 = rRange.aStart.Col(), nCol2 = rRange.aEnd.Col();
    size_t i = nTabFirstId;
    for (const auto& rItem : rData)
    {
        TableTypeRef& pTabData = rDoc.maTables[i];
        if (!pTabData)
            pTabData = std::make_shared<Table>();

        const ScMatrixRef& pMat = rItem.mpRangeData;
        SCSIZE nMatCols, nMatRows;
        pMat->GetDimensions( nMatCols, nMatRows);
        if (nMatCols > o3tl::make_unsigned(nCol2 - nCol1) && nMatRows > o3tl::make_unsigned(nRow2 - nRow1))
        {
            ScMatrix::DoubleOpFunction aDoubleFunc = [=](size_t row, size_t col, double val) -> void
            {
                pTabData->setCell(col + nCol1, row + nRow1, new formula::FormulaDoubleToken(val), 0, false);
            };
            ScMatrix::BoolOpFunction aBoolFunc = [=](size_t row, size_t col, bool val) -> void
            {
                pTabData->setCell(col + nCol1, row + nRow1, new formula::FormulaDoubleToken(val ? 1.0 : 0.0), 0, false);
            };
            ScMatrix::StringOpFunction aStringFunc = [=](size_t row, size_t col, svl::SharedString val) -> void
            {
                pTabData->setCell(col + nCol1, row + nRow1, new formula::FormulaStringToken(std::move(val)), 0, false);
            };
            ScMatrix::EmptyOpFunction aEmptyFunc = [](size_t /*row*/, size_t /*col*/) -> void
            {
                // Nothing. Empty cell.
            };
            pMat->ExecuteOperation(std::pair<size_t, size_t>(0, 0),
                    std::pair<size_t, size_t>(nRow2-nRow1, nCol2-nCol1),
                    std::move(aDoubleFunc), std::move(aBoolFunc), std::move(aStringFunc), std::move(aEmptyFunc));
            // Mark the whole range 'cached'.
            pTabData->setCachedCellRange(nCol1, nRow1, nCol2, nRow2);
        }
        else
        {
            // This may happen due to a matrix not been allocated earlier, in
            // which case it should have exactly one error element.
            SAL_WARN("sc.ui","ScExternalRefCache::setCellRangeData - matrix size mismatch");
            if (nMatCols != 1 || nMatRows != 1)
                SAL_WARN("sc.ui","ScExternalRefCache::setCellRangeData - not a one element matrix");
            else
            {
                FormulaError nErr = GetDoubleErrorValue( pMat->GetDouble(0,0));
                SAL_WARN("sc.ui","ScExternalRefCache::setCellRangeData - matrix error value is " << static_cast<int>(nErr) <<
                        (nErr == FormulaError::MatrixSize ? ", ok" : ", not ok"));
            }
        }
        ++i;
    }

    size_t nTabLastId = nTabFirstId + rRange.aEnd.Tab() - rRange.aStart.Tab();
    ScRange aCacheRange( nCol1, nRow1, static_cast<SCTAB>(nTabFirstId), nCol2, nRow2, static_cast<SCTAB>(nTabLastId));

    rDoc.maRangeArrays.emplace(aCacheRange, pArray);
}

bool ScExternalRefCache::isDocInitialized(sal_uInt16 nFileId)
{
    DocItem* pDoc = getDocItem(nFileId);
    if (!pDoc)
        return false;

    return pDoc->mbInitFromSource;
}

static bool lcl_getStrictTableDataIndex(const ScExternalRefCache::TableNameIndexMap& rMap, const OUString& rName, size_t& rIndex)
{
    ScExternalRefCache::TableNameIndexMap::const_iterator itr = rMap.find(rName);
    if (itr == rMap.end())
        return false;

    rIndex = itr->second;
    return true;
}

bool ScExternalRefCache::DocItem::getTableDataIndex( const OUString& rTabName, size_t& rIndex ) const
{
    ScExternalRefCache::TableNameIndexMap::const_iterator itr = findTableNameIndex(rTabName);
    if (itr == maTableNameIndex.end())
        return false;

    rIndex = itr->second;
    return true;
}

namespace {
OUString getFirstSheetName()
{
    // Get Custom prefix.
    const ScDefaultsOptions& rOpt = ScModule::get()->GetDefaultsOptions();
    // Form sheet name identical to the first generated sheet name when
    // creating an internal document, e.g. 'Sheet1'.
    return rOpt.GetInitTabPrefix() + "1";
}
}

void ScExternalRefCache::initializeDoc(sal_uInt16 nFileId, const vector<OUString>& rTabNames,
        const OUString& rBaseName)
{
    DocItem* pDoc = getDocItem(nFileId);
    if (!pDoc)
        return;

    size_t n = rTabNames.size();

    // table name list - the list must include all table names in the source
    // document and only to be populated when loading the source document, not
    // when loading cached data from, say, Excel XCT/CRN records.
    vector<TableName> aNewTabNames;
    aNewTabNames.reserve(n);
    for (const auto& rTabName : rTabNames)
    {
        TableName aNameItem(ScGlobal::getCharClass().uppercase(rTabName), rTabName);
        aNewTabNames.push_back(aNameItem);
    }
    pDoc->maTableNames.swap(aNewTabNames);

    // data tables - preserve any existing data that may have been set during
    // file import.
    vector<TableTypeRef> aNewTables(n);
    for (size_t i = 0; i < n; ++i)
    {
        size_t nIndex;
        if (lcl_getStrictTableDataIndex(pDoc->maTableNameIndex, pDoc->maTableNames[i].maUpperName, nIndex))
        {
            aNewTables[i] = pDoc->maTables[nIndex];
        }
    }
    pDoc->maTables.swap(aNewTables);

    // name index map
    TableNameIndexMap aNewNameIndex;
    for (size_t i = 0; i < n; ++i)
        aNewNameIndex.emplace(pDoc->maTableNames[i].maUpperName, i);
    pDoc->maTableNameIndex.swap(aNewNameIndex);

    // Setup name for Sheet1 vs base name to be able to load documents
    // that store the base name as table name, or vice versa.
    pDoc->maSingleTableNameAlias.clear();
    if (!rBaseName.isEmpty() && pDoc->maTableNames.size() == 1)
    {
        OUString aSheetName = getFirstSheetName();
        // If the one and only table name matches exactly, carry on the base
        // file name for further alias use. If instead the table name matches
        // the base name, carry on the sheet name as alias.
        if (ScGlobal::GetTransliteration().isEqual( pDoc->maTableNames[0].maRealName, aSheetName))
            pDoc->maSingleTableNameAlias = rBaseName;
        else if (ScGlobal::GetTransliteration().isEqual( pDoc->maTableNames[0].maRealName, rBaseName))
            pDoc->maSingleTableNameAlias = aSheetName;
    }

    pDoc->mbInitFromSource = true;
}

ScExternalRefCache::TableNameIndexMap::const_iterator ScExternalRefCache::DocItem::findTableNameIndex(
        const OUString& rTabName ) const
{
    const OUString aTabNameUpper = ScGlobal::getCharClass().uppercase( rTabName);
    TableNameIndexMap::const_iterator itrTabName = maTableNameIndex.find( aTabNameUpper);
    if (itrTabName != maTableNameIndex.end())
        return itrTabName;

    // Since some time for external references to CSV files the base name is
    // used as sheet name instead of Sheet1, check if we can resolve that.
    // Also helps users that got accustomed to one or the other way.
    if (maSingleTableNameAlias.isEmpty() || maTableNameIndex.size() != 1)
        return itrTabName;

    // maSingleTableNameAlias has been set up only if the original file loaded
    // had exactly one sheet and internal sheet name was Sheet1 or localized or
    // customized equivalent, or base name.
    if (aTabNameUpper == ScGlobal::getCharClass().uppercase( maSingleTableNameAlias))
        return maTableNameIndex.begin();

    return itrTabName;
}

bool ScExternalRefCache::DocItem::getSingleTableNameAlternative( OUString& rTabName ) const
{
    if (maSingleTableNameAlias.isEmpty() || maTableNames.size() != 1)
        return false;
    if (ScGlobal::GetTransliteration().isEqual( rTabName, maTableNames[0].maRealName))
    {
        rTabName = maSingleTableNameAlias;
        return true;
    }
    if (ScGlobal::GetTransliteration().isEqual( rTabName, maSingleTableNameAlias))
    {
        rTabName = maTableNames[0].maRealName;
        return true;
    }
    return false;
}

bool ScExternalRefCache::getSrcDocTable( const ScDocument& rSrcDoc, const OUString& rTabName, SCTAB& rTab,
        sal_uInt16 nFileId ) const
{
    bool bFound = rSrcDoc.GetTable( rTabName, rTab);
    if (!bFound)
    {
        // Check the one table alias alternative.
        const DocItem* pDoc = getDocItem( nFileId );
        if (pDoc)
        {
            OUString aTabName( rTabName);
            if (pDoc->getSingleTableNameAlternative( aTabName))
                bFound = rSrcDoc.GetTable( aTabName, rTab);
        }
    }
    return bFound;
}

OUString ScExternalRefCache::getTableName(sal_uInt16 nFileId, size_t nCacheId) const
{
    if( DocItem* pDoc = getDocItem( nFileId ) )
        if( nCacheId < pDoc->maTableNames.size() )
            return pDoc->maTableNames[ nCacheId ].maRealName;
    return OUString();
}

void ScExternalRefCache::getAllTableNames(sal_uInt16 nFileId, vector<OUString>& rTabNames) const
{
    rTabNames.clear();
    DocItem* pDoc = getDocItem(nFileId);
    if (!pDoc)
        return;

    size_t n = pDoc->maTableNames.size();
    rTabNames.reserve(n);
    for (const auto& rTableName : pDoc->maTableNames)
        rTabNames.push_back(rTableName.maRealName);
}

SCTAB ScExternalRefCache::getTabSpan( sal_uInt16 nFileId, const OUString& rStartTabName, const OUString& rEndTabName ) const
{
    DocItem* pDoc = getDocItem(nFileId);
    if (!pDoc)
        return -1;

    vector<TableName>::const_iterator itrBeg = pDoc->maTableNames.begin();
    vector<TableName>::const_iterator itrEnd = pDoc->maTableNames.end();

    vector<TableName>::const_iterator itrStartTab = ::std::find_if( itrBeg, itrEnd,
            TabNameSearchPredicate( rStartTabName));
    if (itrStartTab == itrEnd)
        return -1;

    vector<TableName>::const_iterator itrEndTab = ::std::find_if( itrBeg, itrEnd,
            TabNameSearchPredicate( rEndTabName));
    if (itrEndTab == itrEnd)
        return 0;

    size_t nStartDist = ::std::distance( itrBeg, itrStartTab);
    size_t nEndDist = ::std::distance( itrBeg, itrEndTab);
    return nStartDist <= nEndDist ? static_cast<SCTAB>(nEndDist - nStartDist + 1) : -static_cast<SCTAB>(nStartDist - nEndDist + 1);
}

void ScExternalRefCache::getAllNumberFormats(vector<sal_uInt32>& rNumFmts) const
{
    std::unique_lock aGuard(maMtxDocs);

    using ::std::sort;
    using ::std::unique;

    vector<sal_uInt32> aNumFmts;
    for (const auto& rEntry : maDocs)
    {
        const vector<TableTypeRef>& rTables = rEntry.second.maTables;
        for (const TableTypeRef& pTab : rTables)
        {
            if (!pTab)
                continue;

            pTab->getAllNumberFormats(aNumFmts);
        }
    }

    // remove duplicates.
    sort(aNumFmts.begin(), aNumFmts.end());
    aNumFmts.erase(unique(aNumFmts.begin(), aNumFmts.end()), aNumFmts.end());
    rNumFmts.swap(aNumFmts);
}

bool ScExternalRefCache::setCacheDocReferenced( sal_uInt16 nFileId )
{
    DocItem* pDocItem = getDocItem(nFileId);
    if (!pDocItem)
        return areAllCacheTablesReferenced();

    for (auto& rxTab : pDocItem->maTables)
    {
        if (rxTab)
            rxTab->setReferenced(true);
    }
    addCacheDocToReferenced( nFileId);
    return areAllCacheTablesReferenced();
}

bool ScExternalRefCache::setCacheTableReferenced( sal_uInt16 nFileId, const OUString& rTabName, size_t nSheets )
{
    DocItem* pDoc = getDocItem(nFileId);
    if (pDoc)
    {
        size_t nIndex = 0;
        if (pDoc->getTableDataIndex( rTabName, nIndex))
        {
            size_t nStop = ::std::min( nIndex + nSheets, pDoc->maTables.size());
            for (size_t i = nIndex; i < nStop; ++i)
            {
                TableTypeRef pTab = pDoc->maTables[i];
                if (pTab)
                {
                    if (!pTab->isReferenced())
                    {
                        pTab->setReferenced(true);
                        addCacheTableToReferenced( nFileId, i);
                    }
                }
            }
        }
    }
    return areAllCacheTablesReferenced();
}

void ScExternalRefCache::setAllCacheTableReferencedStati( bool bReferenced )
{
    std::unique_lock aGuard(maMtxDocs);

    if (bReferenced)
    {
        maReferenced.reset(0);
        for (auto& rEntry : maDocs)
        {
            ScExternalRefCache::DocItem& rDocItem = rEntry.second;
            for (auto& rxTab : rDocItem.maTables)
            {
                if (rxTab)
                    rxTab->setReferenced(true);
            }
        }
    }
    else
    {
        size_t nDocs = 0;
        auto itrMax = std::max_element(maDocs.begin(), maDocs.end(),
            [](const DocDataType::value_type& a, const DocDataType::value_type& b) { return a.first < b.first; });
        if (itrMax != maDocs.end())
            nDocs = itrMax->first + 1;
        maReferenced.reset( nDocs);

        for (auto& [nFileId, rDocItem] : maDocs)
        {
            size_t nTables = rDocItem.maTables.size();
            ReferencedStatus::DocReferenced & rDocReferenced = maReferenced.maDocs[nFileId];
            // All referenced => non-existing tables evaluate as completed.
            rDocReferenced.maTables.resize( nTables, true);
            for (size_t i=0; i < nTables; ++i)
            {
                TableTypeRef & xTab = rDocItem.maTables[i];
                if (xTab)
                {
                    xTab->setReferenced(false);
                    rDocReferenced.maTables[i] = false;
                    rDocReferenced.mbAllTablesReferenced = false;
                    // An addCacheTableToReferenced() actually may have
                    // resulted in mbAllReferenced been set. Clear it.
                    maReferenced.mbAllReferenced = false;
                }
            }
        }
    }
}

void ScExternalRefCache::addCacheTableToReferenced( sal_uInt16 nFileId, size_t nIndex )
{
    if (nFileId >= maReferenced.maDocs.size())
        return;

    ::std::vector<bool> & rTables = maReferenced.maDocs[nFileId].maTables;
    size_t nTables = rTables.size();
    if (nIndex >= nTables)
        return;

    if (!rTables[nIndex])
    {
        rTables[nIndex] = true;
        size_t i = 0;
        while (i < nTables && rTables[i])
            ++i;
        if (i == nTables)
        {
            maReferenced.maDocs[nFileId].mbAllTablesReferenced = true;
            maReferenced.checkAllDocs();
        }
    }
}

void ScExternalRefCache::addCacheDocToReferenced( sal_uInt16 nFileId )
{
    if (nFileId >= maReferenced.maDocs.size())
        return;

    if (!maReferenced.maDocs[nFileId].mbAllTablesReferenced)
    {
        ::std::vector<bool> & rTables = maReferenced.maDocs[nFileId].maTables;
        size_t nSize = rTables.size();
        for (size_t i=0; i < nSize; ++i)
            rTables[i] = true;
        maReferenced.maDocs[nFileId].mbAllTablesReferenced = true;
        maReferenced.checkAllDocs();
    }
}

void ScExternalRefCache::getAllCachedDataSpans( const ScDocument& rSrcDoc, sal_uInt16 nFileId, sc::ColumnSpanSet& rSet ) const
{
    const DocItem* pDocItem = getDocItem(nFileId);
    if (!pDocItem)
        // This document is not cached.
        return;

    const std::vector<TableTypeRef>& rTables = pDocItem->maTables;
    for (size_t nTab = 0, nTabCount = rTables.size(); nTab < nTabCount; ++nTab)
    {
        TableTypeRef pTab = rTables[nTab];
        if (!pTab)
            continue;

        std::vector<SCROW> aRows;
        pTab->getAllRows(aRows);
        for (SCROW nRow : aRows)
        {
            std::vector<SCCOL> aCols;
            pTab->getAllCols(nRow, aCols);
            for (SCCOL nCol : aCols)
            {
                rSet.set(rSrcDoc, nTab, nCol, nRow, true);
            }
        }
    }
}

ScExternalRefCache::ReferencedStatus::ReferencedStatus() :
    mbAllReferenced(false)
{
    reset(0);
}

void ScExternalRefCache::ReferencedStatus::reset( size_t nDocs )
{
    if (nDocs)
    {
        mbAllReferenced = false;
        DocReferencedVec aRefs( nDocs);
        maDocs.swap( aRefs);
    }
    else
    {
        mbAllReferenced = true;
        DocReferencedVec aRefs;
        maDocs.swap( aRefs);
    }
}

void ScExternalRefCache::ReferencedStatus::checkAllDocs()
{
    if (std::all_of(maDocs.begin(), maDocs.end(), [](const DocReferenced& rDoc) { return rDoc.mbAllTablesReferenced; }))
        mbAllReferenced = true;
}

ScExternalRefCache::TableTypeRef ScExternalRefCache::getCacheTable(sal_uInt16 nFileId, size_t nTabIndex) const
{
    DocItem* pDoc = getDocItem(nFileId);
    if (!pDoc || nTabIndex >= pDoc->maTables.size())
        return TableTypeRef();

    return pDoc->maTables[nTabIndex];
}

ScExternalRefCache::TableTypeRef ScExternalRefCache::getCacheTable(sal_uInt16 nFileId, const OUString& rTabName,
        bool bCreateNew, size_t* pnIndex, const OUString* pExtUrl)
{
    // In API, the index is transported as cached sheet ID of type sal_Int32 in
    // sheet::SingleReference.Sheet or sheet::ComplexReference.Reference1.Sheet
    // in a sheet::FormulaToken, choose a sensible value for N/A. Effectively
    // being 0xffffffff
    const size_t nNotAvailable = static_cast<size_t>( static_cast<sal_Int32>( -1));

    DocItem* pDoc = getDocItem(nFileId);
    if (!pDoc)
    {
        if (pnIndex) *pnIndex = nNotAvailable;
        return TableTypeRef();
    }

    DocItem& rDoc = *pDoc;

    size_t nIndex;
    if (rDoc.getTableDataIndex(rTabName, nIndex))
    {
        // specified table found.
        if( pnIndex ) *pnIndex = nIndex;
        if (bCreateNew && !rDoc.maTables[nIndex])
            rDoc.maTables[nIndex] = std::make_shared<Table>();

        return rDoc.maTables[nIndex];
    }

    if (!bCreateNew)
    {
        if (pnIndex) *pnIndex = nNotAvailable;
        return TableTypeRef();
    }

    // If this is the first table to be created propagate the base name or
    // Sheet1 as an alias. For subsequent tables remove it again.
    if (rDoc.maTableNames.empty())
    {
        if (pExtUrl)
        {
            const OUString aBaseName( INetURLObject( *pExtUrl).GetBase());
            const OUString aSheetName( getFirstSheetName());
            if (ScGlobal::GetTransliteration().isEqual( rTabName, aSheetName))
                pDoc->maSingleTableNameAlias = aBaseName;
            else if (ScGlobal::GetTransliteration().isEqual( rTabName, aBaseName))
                pDoc->maSingleTableNameAlias = aSheetName;
        }
    }
    else
    {
        rDoc.maSingleTableNameAlias.clear();
    }

    // Specified table doesn't exist yet.  Create one.
    OUString aTabNameUpper = ScGlobal::getCharClass().uppercase(rTabName);
    nIndex = rDoc.maTables.size();
    if( pnIndex ) *pnIndex = nIndex;
    TableTypeRef pTab = std::make_shared<Table>();
    rDoc.maTables.push_back(pTab);
    rDoc.maTableNames.emplace_back(aTabNameUpper, rTabName);
    rDoc.maTableNameIndex.emplace(aTabNameUpper, nIndex);
    return pTab;
}

void ScExternalRefCache::clearCache(sal_uInt16 nFileId)
{
    std::unique_lock aGuard(maMtxDocs);
    maDocs.erase(nFileId);
}

void ScExternalRefCache::clearCacheTables(sal_uInt16 nFileId)
{
    std::unique_lock aGuard(maMtxDocs);
    DocItem* pDocItem = getDocItem(aGuard, nFileId);
    if (!pDocItem)
        // This document is not cached at all.
        return;

    // Clear all cache table content, but keep the tables.
    std::vector<TableTypeRef>& rTabs = pDocItem->maTables;
    for (TableTypeRef & pTab : rTabs)
    {
        if (!pTab)
            continue;

        pTab->clear();
    }

    // Clear the external range name caches.
    pDocItem->maRangeNames.clear();
    pDocItem->maRangeArrays.clear();
    pDocItem->maRealRangeNameMap.clear();
}

ScExternalRefCache::DocItem* ScExternalRefCache::getDocItem(sal_uInt16 nFileId) const
{
    std::unique_lock aGuard(maMtxDocs);
    return getDocItem(aGuard, nFileId);
}

ScExternalRefCache::DocItem* ScExternalRefCache::getDocItem(std::unique_lock<std::mutex>& /*rGuard*/, sal_uInt16 nFileId) const
{

    using ::std::pair;
    DocDataType::iterator itrDoc = maDocs.find(nFileId);
    if (itrDoc == maDocs.end())
    {
        // specified document is not cached.
        pair<DocDataType::iterator, bool> res = maDocs.emplace(
                nFileId, DocItem());

        if (!res.second)
            // insertion failed.
            return nullptr;

        itrDoc = res.first;
    }

    return &itrDoc->second;
}

ScExternalRefLink::ScExternalRefLink(ScDocument& rDoc, sal_uInt16 nFileId) :
    ::sfx2::SvBaseLink(::SfxLinkUpdateMode::ONCALL, SotClipboardFormatId::SIMPLE_FILE),
    mnFileId(nFileId),
    mrDoc(rDoc),
    mbDoRefresh(true)
{
}

ScExternalRefLink::~ScExternalRefLink()
{
}

void ScExternalRefLink::Closed()
{
    ScExternalRefManager* pMgr = mrDoc.GetExternalRefManager();
    pMgr->breakLink(mnFileId);
}

::sfx2::SvBaseLink::UpdateResult ScExternalRefLink::DataChanged(const OUString& /*rMimeType*/, const Any& /*rValue*/)
{
    if (!mbDoRefresh)
        return SUCCESS;

    OUString aFile, aFilter;
    sfx2::LinkManager::GetDisplayNames(this, nullptr, &aFile, nullptr, &aFilter);
    ScExternalRefManager* pMgr = mrDoc.GetExternalRefManager();

    if (!pMgr->isFileLoadable(aFile))
        return ERROR_GENERAL;

    const OUString* pCurFile = pMgr->getExternalFileName(mnFileId);
    if (!pCurFile)
        return ERROR_GENERAL;

    if (*pCurFile == aFile)
    {
        // Refresh the current source document.
        if (!pMgr->refreshSrcDocument(mnFileId))
            return ERROR_GENERAL;
    }
    else
    {
        // The source document has changed.
        ScViewData* pViewData = ScDocShell::GetViewData();
        if (!pViewData)
            return ERROR_GENERAL;

        ScDocShell& rDocShell = pViewData->GetDocShell();
        ScDocShellModificator aMod(rDocShell);
        pMgr->switchSrcFile(mnFileId, aFile, aFilter);
        aMod.SetDocumentModified();
    }

    return SUCCESS;
}

void ScExternalRefLink::Edit(weld::Window* pParent, const Link<SvBaseLink&,void>& /*rEndEditHdl*/)
{
    SvBaseLink::Edit(pParent, Link<SvBaseLink&,void>());
}

void ScExternalRefLink::SetDoRefresh(bool b)
{
    mbDoRefresh = b;
}

static FormulaToken* convertToToken( ScDocument& rHostDoc, const ScDocument& rSrcDoc, ScRefCellValue& rCell )
{
    if (rCell.hasEmptyValue())
    {
        bool bInherited = (rCell.getType() == CELLTYPE_FORMULA);
        return new ScEmptyCellToken(bInherited, false);
    }

    switch (rCell.getType())
    {
        case CELLTYPE_EDIT:
        case CELLTYPE_STRING:
        {
            OUString aStr = rCell.getString(rSrcDoc);
            svl::SharedString aSS = rHostDoc.GetSharedStringPool().intern(aStr);
            return new formula::FormulaStringToken(std::move(aSS));
        }
        case CELLTYPE_VALUE:
            return new formula::FormulaDoubleToken(rCell.getDouble());
        case CELLTYPE_FORMULA:
        {
            ScFormulaCell* pFCell = rCell.getFormula();
            FormulaError nError = pFCell->GetErrCode();
            if (nError != FormulaError::NONE)
                return new FormulaErrorToken( nError);
            else if (pFCell->IsValue())
            {
                double fVal = pFCell->GetValue();
                return new formula::FormulaDoubleToken(fVal);
            }
            else
            {
                svl::SharedString aSS = rHostDoc.GetSharedStringPool().intern( pFCell->GetString().getString());
                return new formula::FormulaStringToken(std::move(aSS));
            }
        }
        default:
            OSL_FAIL("attempted to convert an unknown cell type.");
    }

    return nullptr;
}

static std::unique_ptr<ScTokenArray> convertToTokenArray(
    ScDocument& rHostDoc, const ScDocument& rSrcDoc, ScRange& rRange, vector<ScExternalRefCache::SingleRangeData>& rCacheData )
{
    ScAddress& s = rRange.aStart;
    ScAddress& e = rRange.aEnd;

    const SCTAB nTab1 = s.Tab(), nTab2 = e.Tab();
    const SCCOL nCol1 = s.Col(), nCol2 = e.Col();
    const SCROW nRow1 = s.Row(), nRow2 = e.Row();

    if (nTab2 != nTab1)
        // For now, we don't support multi-sheet ranges intentionally because
        // we don't have a way to express them in a single token.  In the
        // future we can introduce a new stack variable type svMatrixList with
        // a new token type that can store a 3D matrix value and convert a 3D
        // range to it.
        return nullptr;

    std::unique_ptr<ScRange> pUsedRange;

    unique_ptr<ScTokenArray> pArray(new ScTokenArray(rSrcDoc));
    bool bFirstTab = true;
    vector<ScExternalRefCache::SingleRangeData>::iterator
        itrCache = rCacheData.begin(), itrCacheEnd = rCacheData.end();

    for (SCTAB nTab = nTab1; nTab <= nTab2 && itrCache != itrCacheEnd; ++nTab, ++itrCache)
    {
        // Only loop within the data area.
        SCCOL nDataCol1 = nCol1, nDataCol2 = nCol2;
        SCROW nDataRow1 = nRow1, nDataRow2 = nRow2;
        bool bShrunk;
        if (!rSrcDoc.ShrinkToUsedDataArea( bShrunk, nTab, nDataCol1, nDataRow1, nDataCol2, nDataRow2, false))
            // no data within specified range.
            continue;

        if (pUsedRange)
            // Make sure the used area only grows, not shrinks.
            pUsedRange->ExtendTo(ScRange(nDataCol1, nDataRow1, 0, nDataCol2, nDataRow2, 0));
        else
            pUsedRange.reset(new ScRange(nDataCol1, nDataRow1, 0, nDataCol2, nDataRow2, 0));

        SCSIZE nMatrixColumns = static_cast<SCSIZE>(nCol2-nCol1+1);
        SCSIZE nMatrixRows = static_cast<SCSIZE>(nRow2-nRow1+1);
        ScMatrixRef xMat = new ScMatrix( nMatrixColumns, nMatrixRows);

        // Check if size could be allocated and if not skip the fill, there's
        // one error element instead. But retry first with the actual data area
        // if that is smaller than the original range, which works for most
        // functions just not some that operate/compare with the original size
        // and expect empty values in non-data areas.
        // Restrict this though to ranges of entire columns or rows, other
        // ranges might be on purpose. (Other special cases to handle?)
        /* TODO: sparse matrix could help */
        SCSIZE nMatCols, nMatRows;
        xMat->GetDimensions( nMatCols, nMatRows);
        if (nMatCols == nMatrixColumns && nMatRows == nMatrixRows)
        {
            rSrcDoc.FillMatrix(*xMat, nTab, nCol1, nRow1, nCol2, nRow2, &rHostDoc.GetSharedStringPool());
        }
        else if ((nCol1 == 0 && nCol2 == rSrcDoc.MaxCol()) || (nRow1 == 0 && nRow2 == rSrcDoc.MaxRow()))
        {
            if ((o3tl::make_unsigned(nDataCol2-nDataCol1+1) < nMatrixColumns) ||
                (o3tl::make_unsigned(nDataRow2-nDataRow1+1) < nMatrixRows))
            {
                nMatrixColumns = static_cast<SCSIZE>(nDataCol2-nDataCol1+1);
                nMatrixRows = static_cast<SCSIZE>(nDataRow2-nDataRow1+1);
                xMat = new ScMatrix( nMatrixColumns, nMatrixRows);
                xMat->GetDimensions( nMatCols, nMatRows);
                if (nMatCols == nMatrixColumns && nMatRows == nMatrixRows)
                    rSrcDoc.FillMatrix(*xMat, nTab, nDataCol1, nDataRow1, nDataCol2, nDataRow2, &rHostDoc.GetSharedStringPool());
            }
        }

        if (!bFirstTab)
            pArray->AddOpCode(ocSep);

        ScMatrixToken aToken(xMat);
        pArray->AddToken(aToken);

        itrCache->mpRangeData = std::move(xMat);

        bFirstTab = false;
    }

    if (!pUsedRange)
        return nullptr;

    s.SetCol(pUsedRange->aStart.Col());
    s.SetRow(pUsedRange->aStart.Row());
    e.SetCol(pUsedRange->aEnd.Col());
    e.SetRow(pUsedRange->aEnd.Row());

    return pArray;
}

static std::unique_ptr<ScTokenArray> lcl_fillEmptyMatrix(const ScDocument& rDoc, const ScRange& rRange)
{
    SCSIZE nC = static_cast<SCSIZE>(rRange.aEnd.Col()-rRange.aStart.Col()+1);
    SCSIZE nR = static_cast<SCSIZE>(rRange.aEnd.Row()-rRange.aStart.Row()+1);
    ScMatrixRef xMat = new ScMatrix(nC, nR);

    ScMatrixToken aToken(std::move(xMat));
    unique_ptr<ScTokenArray> pArray(new ScTokenArray(rDoc));
    pArray->AddToken(aToken);
    return pArray;
}

namespace {
bool isLinkUpdateAllowedInDoc(const ScDocument& rDoc)
{
    ScDocShell* pDocShell = rDoc.GetDocumentShell();
    if (!pDocShell)
        return rDoc.IsFunctionAccess();

    return pDocShell->GetEmbeddedObjectContainer().getUserAllowsLinkUpdate();
}
}

ScExternalRefManager::ScExternalRefManager(ScDocument& rDoc) :
    mrDoc(rDoc),
    maRefCache(rDoc),
    mbInReferenceMarking(false),
    mbUserInteractionEnabled(true),
    mbDocTimerEnabled(true),
    maSrcDocTimer( "sc::ScExternalRefManager maSrcDocTimer" )
{
    maSrcDocTimer.SetInvokeHandler( LINK(this, ScExternalRefManager, TimeOutHdl) );
    maSrcDocTimer.SetTimeout(SRCDOC_SCAN_INTERVAL);
}

ScExternalRefManager::~ScExternalRefManager()
{
    clear();
}

OUString ScExternalRefManager::getCacheTableName(sal_uInt16 nFileId, size_t nTabIndex) const
{
    return maRefCache.getTableName(nFileId, nTabIndex);
}

ScExternalRefCache::TableTypeRef ScExternalRefManager::getCacheTable(sal_uInt16 nFileId, size_t nTabIndex) const
{
    return maRefCache.getCacheTable(nFileId, nTabIndex);
}

ScExternalRefCache::TableTypeRef ScExternalRefManager::getCacheTable(
    sal_uInt16 nFileId, const OUString& rTabName, bool bCreateNew, size_t* pnIndex, const OUString* pExtUrl)
{
    return maRefCache.getCacheTable(nFileId, rTabName, bCreateNew, pnIndex, pExtUrl);
}

ScExternalRefManager::LinkListener::LinkListener()
{
}

ScExternalRefManager::LinkListener::~LinkListener()
{
}

ScExternalRefManager::ApiGuard::ApiGuard(const ScDocument& rDoc) :
    mpMgr(rDoc.GetExternalRefManager()),
    mbOldInteractionEnabled(mpMgr->mbUserInteractionEnabled)
{
    // We don't want user interaction handled in the API.
    mpMgr->mbUserInteractionEnabled = false;
}

ScExternalRefManager::ApiGuard::~ApiGuard()
{
    // Restore old value.
    mpMgr->mbUserInteractionEnabled = mbOldInteractionEnabled;
}

void ScExternalRefManager::getAllCachedTableNames(sal_uInt16 nFileId, vector<OUString>& rTabNames) const
{
    maRefCache.getAllTableNames(nFileId, rTabNames);
}

SCTAB ScExternalRefManager::getCachedTabSpan( sal_uInt16 nFileId, const OUString& rStartTabName, const OUString& rEndTabName ) const
{
    return maRefCache.getTabSpan( nFileId, rStartTabName, rEndTabName);
}

void ScExternalRefManager::getAllCachedNumberFormats(vector<sal_uInt32>& rNumFmts) const
{
    maRefCache.getAllNumberFormats(rNumFmts);
}

sal_uInt16 ScExternalRefManager::getExternalFileCount() const
{
    return static_cast< sal_uInt16 >( maSrcFiles.size() );
}

void ScExternalRefManager::markUsedByLinkListeners()
{
    bool bAllMarked = false;
    for (const auto& [rFileId, rLinkListeners] : maLinkListeners)
    {
        if (!rLinkListeners.empty())
            bAllMarked = maRefCache.setCacheDocReferenced(rFileId);

        if (bAllMarked)
            break;
        /* TODO: LinkListeners should remember the table they're listening to.
         * As is, listening to one table will mark all tables of the document
         * being referenced. */
    }
}

void ScExternalRefManager::markUsedExternalRefCells()
{
    for (const auto& rEntry : maRefCells)
    {
        for (ScFormulaCell* pCell : rEntry.second)
        {
            bool bUsed = pCell->MarkUsedExternalReferences();
            if (bUsed)
                // Return true when at least one cell references external docs.
                return;
        }
    }
}

bool ScExternalRefManager::setCacheTableReferenced( sal_uInt16 nFileId, const OUString& rTabName, size_t nSheets )
{
    return maRefCache.setCacheTableReferenced( nFileId, rTabName, nSheets);
}

void ScExternalRefManager::setAllCacheTableReferencedStati( bool bReferenced )
{
    mbInReferenceMarking = !bReferenced;
    maRefCache.setAllCacheTableReferencedStati( bReferenced );
}

void ScExternalRefManager::storeRangeNameTokens(sal_uInt16 nFileId, const OUString& rName, const ScTokenArray& rArray)
{
    ScExternalRefCache::TokenArrayRef pNewArray;
    if (!rArray.HasExternalRef())
    {
        // Parse all tokens in this external range data, and replace each absolute
        // reference token with an external reference token, and cache them.
        pNewArray = std::make_shared<ScTokenArray>(mrDoc);
        FormulaTokenArrayPlainIterator aIter(rArray);
        for (const FormulaToken* pToken = aIter.First(); pToken; pToken = aIter.Next())
        {
            bool bTokenAdded = false;
            switch (pToken->GetType())
            {
                case svSingleRef:
                {
                    const ScSingleRefData& rRef = *pToken->GetSingleRef();
                    OUString aTabName;
                    if (SCTAB nCacheId = rRef.Tab(); nCacheId >= 0)
                        aTabName = maRefCache.getTableName(nFileId, nCacheId);
                    ScExternalSingleRefToken aNewToken(nFileId, svl::SharedString(aTabName),   // string not interned
                        *pToken->GetSingleRef());
                    pNewArray->AddToken(aNewToken);
                    bTokenAdded = true;
                }
                break;
                case svDoubleRef:
                {
                    const ScSingleRefData& rRef = *pToken->GetSingleRef();
                    OUString aTabName;
                    if (SCTAB nCacheId = rRef.Tab(); nCacheId >= 0)
                        aTabName = maRefCache.getTableName(nFileId, nCacheId);
                    ScExternalDoubleRefToken aNewToken(nFileId, svl::SharedString(aTabName),   // string not interned
                        *pToken->GetDoubleRef());
                    pNewArray->AddToken(aNewToken);
                    bTokenAdded = true;
                }
                break;
                default:
                    ;   // nothing
            }

            if (!bTokenAdded)
                pNewArray->AddToken(*pToken);
        }
    }
    else
        pNewArray = rArray.Clone();

    maRefCache.setRangeNameTokens(nFileId, rName, pNewArray);
}

namespace {

/**
 * Put a single cell data into internal cache table.
 *
 * @param pFmt optional cell format index that may need to be stored with
 *             the cell value.
 */
void putCellDataIntoCache(
    ScExternalRefCache& rRefCache, const ScExternalRefCache::TokenRef& pToken,
    sal_uInt16 nFileId, const OUString& rTabName, const ScAddress& rCell,
    const ScExternalRefCache::CellFormat* pFmt)
{
    // Now, insert the token into cache table but don't cache empty cells.
    if (pToken->GetType() != formula::svEmptyCell)
    {
        sal_uLong nFmtIndex = (pFmt && pFmt->mbIsSet) ? pFmt->mnIndex : 0;
        rRefCache.setCellData(nFileId, rTabName, rCell.Col(), rCell.Row(), pToken, nFmtIndex);
    }
}

/**
 * Put the data into our internal cache table.
 *
 * @param rRefCache cache table set.
 * @param pArray single range data to be returned.
 * @param nFileId external file ID
 * @param rTabName name of the table where the data should be cached.
 * @param rCacheData range data to be cached.
 * @param rCacheRange original cache range, including the empty region if
 *                    any.
 * @param rDataRange reduced cache range that includes only the non-empty
 *                   data area.
 */
void putRangeDataIntoCache(
    ScExternalRefCache& rRefCache, ScExternalRefCache::TokenArrayRef& pArray,
    sal_uInt16 nFileId, const OUString& rTabName,
    const vector<ScExternalRefCache::SingleRangeData>& rCacheData,
    const ScRange& rCacheRange, const ScRange& rDataRange)
{
    if (pArray)
        // Cache these values.
        rRefCache.setCellRangeData(nFileId, rDataRange, rCacheData, pArray);
    else
    {
        // Array is empty.  Fill it with an empty matrix of the required size.
        pArray = lcl_fillEmptyMatrix(rRefCache.getDoc(), rCacheRange);

        // Make sure to set this range 'cached', to prevent unnecessarily
        // accessing the src document time and time again.
        ScExternalRefCache::TableTypeRef pCacheTab =
            rRefCache.getCacheTable(nFileId, rTabName, true, nullptr, nullptr);
        if (pCacheTab)
            pCacheTab->setCachedCellRange(
                rCacheRange.aStart.Col(), rCacheRange.aStart.Row(), rCacheRange.aEnd.Col(), rCacheRange.aEnd.Row());
    }
}

/**
 * When accessing an external document for the first time, we need to
 * populate the cache with all its sheet names (whether they are referenced
 * or not) in the correct order.  Many client codes that use external
 * references make this assumption.
 *
 * @param rRefCache cache table set.
 * @param pSrcDoc source document instance.
 * @param nFileId external file ID associated with the source document.
 */
void initDocInCache(ScExternalRefCache& rRefCache, const ScDocument* pSrcDoc, sal_uInt16 nFileId)
{
    if (!pSrcDoc)
        return;

    if (rRefCache.isDocInitialized(nFileId))
        // Already initialized.  No need to do this twice.
        return;

    SCTAB nTabCount = pSrcDoc->GetTableCount();
    if (!nTabCount)
        return;

    // Populate the cache with all table names in the source document.
    vector<OUString> aTabNames;
    aTabNames.reserve(nTabCount);
    for (SCTAB i = 0; i < nTabCount; ++i)
    {
        OUString aName;
        pSrcDoc->GetName(i, aName);
        aTabNames.push_back(aName);
    }

    // Obtain the base name, don't bother if there are more than one sheets.
    OUString aBaseName;
    if (nTabCount == 1)
    {
        const ScDocShell* pShell = pSrcDoc->GetDocumentShell();
        if (pShell && pShell->GetMedium())
        {
            OUString aName = pShell->GetMedium()->GetName();
            aBaseName = INetURLObject( aName).GetBase();
        }
    }

    rRefCache.initializeDoc(nFileId, aTabNames, aBaseName);
}

}

bool ScExternalRefManager::getSrcDocTable( const ScDocument& rSrcDoc, const OUString& rTabName, SCTAB& rTab,
        sal_uInt16 nFileId ) const
{
    return maRefCache.getSrcDocTable( rSrcDoc, rTabName, rTab, nFileId);
}

ScExternalRefCache::TokenRef ScExternalRefManager::getSingleRefToken(
    sal_uInt16 nFileId, const OUString& rTabName, const ScAddress& rCell,
    const ScAddress* pCurPos, SCTAB* pTab, ScExternalRefCache::CellFormat* pFmt)
{
    if (pCurPos)
        insertRefCell(nFileId, *pCurPos);

    maybeLinkExternalFile(nFileId);

    if (pTab)
        *pTab = -1;

    if (pFmt)
        pFmt->mbIsSet = false;

    ScDocument* pSrcDoc = getInMemorySrcDocument(nFileId);
    if (pSrcDoc)
    {
        // source document already loaded in memory.  Re-use this instance.
        SCTAB nTab;
        if (!getSrcDocTable( *pSrcDoc, rTabName, nTab, nFileId))
        {
            // specified table name doesn't exist in the source document.
            ScExternalRefCache::TokenRef pToken(new FormulaErrorToken(FormulaError::NoRef));
            return pToken;
        }

        if (pTab)
            *pTab = nTab;

        ScExternalRefCache::TokenRef pToken =
            getSingleRefTokenFromSrcDoc(
                nFileId, *pSrcDoc, ScAddress(rCell.Col(),rCell.Row(),nTab), pFmt);

        putCellDataIntoCache(maRefCache, pToken, nFileId, rTabName, rCell, pFmt);
        return pToken;
    }

    // Check if the given table name and the cell position is cached.
    sal_uInt32 nFmtIndex = 0;
    ScExternalRefCache::TokenRef pToken = maRefCache.getCellData(
        nFileId, rTabName, rCell.Col(), rCell.Row(), &nFmtIndex);
    if (pToken)
    {
        // Cache hit !
        fillCellFormat(nFmtIndex, pFmt);
        return pToken;
    }

    // reference not cached.  read from the source document.
    pSrcDoc = getSrcDocument(nFileId);
    if (!pSrcDoc)
    {
        // Source document not reachable.
        if (!isLinkUpdateAllowedInDoc(mrDoc))
        {
            // Indicate with specific error.
            pToken.reset(new FormulaErrorToken(FormulaError::LinkFormulaNeedingCheck));
        }
        else
        {
            // Throw a reference error.
            pToken.reset(new FormulaErrorToken(FormulaError::NoRef));
        }
        return pToken;
    }

    SCTAB nTab;
    if (!getSrcDocTable( *pSrcDoc, rTabName, nTab, nFileId))
    {
        // specified table name doesn't exist in the source document.
        pToken.reset(new FormulaErrorToken(FormulaError::NoRef));
        return pToken;
    }

    if (pTab)
        *pTab = nTab;

    SCCOL nDataCol1 = 0, nDataCol2 = pSrcDoc->MaxCol();
    SCROW nDataRow1 = 0, nDataRow2 = pSrcDoc->MaxRow();
    bool bData = pSrcDoc->ShrinkToDataArea(nTab, nDataCol1, nDataRow1, nDataCol2, nDataRow2);
    if (!bData || rCell.Col() < nDataCol1 || nDataCol2 < rCell.Col() || rCell.Row() < nDataRow1 || nDataRow2 < rCell.Row())
    {
        // requested cell is outside the data area.  Don't even bother caching
        // this data, but add it to the cached range to prevent accessing the
        // source document time and time again.
        ScExternalRefCache::TableTypeRef pCacheTab =
            maRefCache.getCacheTable(nFileId, rTabName, true, nullptr, nullptr);
        if (pCacheTab)
            pCacheTab->setCachedCell(rCell.Col(), rCell.Row());

        pToken.reset(new ScEmptyCellToken(false, false));
        return pToken;
    }

    pToken = getSingleRefTokenFromSrcDoc(
        nFileId, *pSrcDoc, ScAddress(rCell.Col(),rCell.Row(),nTab), pFmt);

    putCellDataIntoCache(maRefCache, pToken, nFileId, rTabName, rCell, pFmt);
    return pToken;
}

ScExternalRefCache::TokenArrayRef ScExternalRefManager::getDoubleRefTokens(
    sal_uInt16 nFileId, const OUString& rTabName, const ScRange& rRange, const ScAddress* pCurPos)
{
    if (pCurPos)
        insertRefCell(nFileId, *pCurPos);

    maybeLinkExternalFile(nFileId);

    ScRange aDataRange(rRange);
    ScDocument* pSrcDoc = getInMemorySrcDocument(nFileId);
    if (pSrcDoc)
    {
        // Document already loaded in memory.
        vector<ScExternalRefCache::SingleRangeData> aCacheData;
        ScExternalRefCache::TokenArrayRef pArray =
            getDoubleRefTokensFromSrcDoc(*pSrcDoc, rTabName, aDataRange, aCacheData);

        // Put the data into cache.
        putRangeDataIntoCache(maRefCache, pArray, nFileId, rTabName, aCacheData, rRange, aDataRange);
        return pArray;
    }

    // Check if the given table name and the cell position is cached.
    ScExternalRefCache::TokenArrayRef pArray =
        maRefCache.getCellRangeData(nFileId, rTabName, rRange);
    if (pArray)
        // Cache hit !
        return pArray;

    pSrcDoc = getSrcDocument(nFileId);
    if (!pSrcDoc)
    {
        // Source document is not reachable.  Throw a reference error.
        pArray = std::make_shared<ScTokenArray>(maRefCache.getDoc());
        pArray->AddToken(FormulaErrorToken(FormulaError::NoRef));
        return pArray;
    }

    vector<ScExternalRefCache::SingleRangeData> aCacheData;
    pArray = getDoubleRefTokensFromSrcDoc(*pSrcDoc, rTabName, aDataRange, aCacheData);

    // Put the data into cache.
    putRangeDataIntoCache(maRefCache, pArray, nFileId, rTabName, aCacheData, rRange, aDataRange);
    return pArray;
}

ScExternalRefCache::TokenArrayRef ScExternalRefManager::getRangeNameTokens(
    sal_uInt16 nFileId, const OUString& rName, const ScAddress* pCurPos)
{
    if (pCurPos)
        insertRefCell(nFileId, *pCurPos);

    maybeLinkExternalFile(nFileId);

    OUString aName = rName; // make a copy to have the casing corrected.
    ScDocument* pSrcDoc = getInMemorySrcDocument(nFileId);
    if (pSrcDoc)
    {
        // Document already loaded in memory.
        ScExternalRefCache::TokenArrayRef pArray =
            getRangeNameTokensFromSrcDoc(nFileId, *pSrcDoc, aName);

        if (pArray)
            // Cache this range name array.
            maRefCache.setRangeNameTokens(nFileId, aName, pArray);

        return pArray;
    }

    ScExternalRefCache::TokenArrayRef pArray = maRefCache.getRangeNameTokens(nFileId, rName);
    if (pArray)
        // This range name is cached.
        return pArray;

    pSrcDoc = getSrcDocument(nFileId);
    if (!pSrcDoc)
        // failed to load document from disk.
        return ScExternalRefCache::TokenArrayRef();

    pArray = getRangeNameTokensFromSrcDoc(nFileId, *pSrcDoc, aName);

    if (pArray)
        // Cache this range name array.
        maRefCache.setRangeNameTokens(nFileId, aName, pArray);

    return pArray;
}

namespace {

bool hasRangeName(const ScDocument& rDoc, const OUString& rName)
{
    ScRangeName* pExtNames = rDoc.GetRangeName();
    OUString aUpperName = ScGlobal::getCharClass().uppercase(rName);
    const ScRangeData* pRangeData = pExtNames->findByUpperName(aUpperName);
    return pRangeData != nullptr;
}

}

bool ScExternalRefManager::isValidRangeName(sal_uInt16 nFileId, const OUString& rName)
{
    maybeLinkExternalFile(nFileId);
    ScDocument* pSrcDoc = getInMemorySrcDocument(nFileId);
    if (pSrcDoc)
    {
        // Only check the presence of the name.
        if (hasRangeName(*pSrcDoc, rName))
        {
            maRefCache.setRangeName(nFileId, rName);
            return true;
        }
        return false;
    }

    if (maRefCache.isValidRangeName(nFileId, rName))
        // Range name is cached.
        return true;

    pSrcDoc = getSrcDocument(nFileId);
    if (!pSrcDoc)
        // failed to load document from disk.
        return false;

    if (hasRangeName(*pSrcDoc, rName))
    {
        maRefCache.setRangeName(nFileId, rName);
        return true;
    }

    return false;
}

void ScExternalRefManager::refreshAllRefCells(sal_uInt16 nFileId)
{
    RefCellMap::iterator itrFile = maRefCells.find(nFileId);
    if (itrFile == maRefCells.end())
        return;

    RefCellSet& rRefCells = itrFile->second;
    for_each(rRefCells.begin(), rRefCells.end(), UpdateFormulaCell());

    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
        return;

    ScTabViewShell* pVShell = pViewData->GetViewShell();
    if (!pVShell)
        return;

    // Repainting the grid also repaints the texts, but is there a better way
    // to refresh texts?
    pVShell->Invalidate(FID_REPAINT);
    pVShell->PaintGrid();
}

namespace {

void insertRefCellByIterator(
    const ScExternalRefManager::RefCellMap::iterator& itr, ScFormulaCell* pCell)
{
    if (pCell)
    {
        itr->second.insert(pCell);
        pCell->SetIsExtRef();
    }
}

}

void ScExternalRefManager::insertRefCell(sal_uInt16 nFileId, const ScAddress& rCell)
{
    RefCellMap::iterator itr = maRefCells.find(nFileId);
    if (itr == maRefCells.end())
    {
        RefCellSet aRefCells;
        pair<RefCellMap::iterator, bool> r = maRefCells.emplace(
            nFileId, aRefCells);
        if (!r.second)
            // insertion failed.
            return;

        itr = r.first;
    }

    insertRefCellByIterator(itr, mrDoc.GetFormulaCell(rCell));
}

void ScExternalRefManager::insertRefCellFromTemplate( ScFormulaCell* pTemplateCell, ScFormulaCell* pCell )
{
    if (!pTemplateCell || !pCell)
        return;

    for (RefCellMap::iterator itr = maRefCells.begin(); itr != maRefCells.end(); ++itr)
    {
        if (itr->second.find(pTemplateCell) != itr->second.end())
            insertRefCellByIterator(itr, pCell);
    }
}

bool ScExternalRefManager::hasCellExternalReference(const ScAddress& rCell)
{
    ScFormulaCell* pCell = mrDoc.GetFormulaCell(rCell);

    if (pCell)
        return std::any_of(maRefCells.begin(), maRefCells.end(),
            [&pCell](const RefCellMap::value_type& rEntry) { return rEntry.second.find(pCell) != rEntry.second.end(); });

    return false;
}

void ScExternalRefManager::enableDocTimer( bool bEnable )
{
    if (mbDocTimerEnabled == bEnable)
        return;

    mbDocTimerEnabled = bEnable;
    if (mbDocTimerEnabled)
    {
        if (!maDocShells.empty())
        {
            for (auto& rEntry : maDocShells)
                rEntry.second.maLastAccess = tools::Time(tools::Time::SYSTEM);

            maSrcDocTimer.Start();
        }
    }
    else
        maSrcDocTimer.Stop();
}

void ScExternalRefManager::fillCellFormat(sal_uLong nFmtIndex, ScExternalRefCache::CellFormat* pFmt) const
{
    if (!pFmt)
        return;

    SvNumFormatType nFmtType = mrDoc.GetFormatTable()->GetType(nFmtIndex);
    if (nFmtType != SvNumFormatType::UNDEFINED)
    {
        pFmt->mbIsSet = true;
        pFmt->mnIndex = nFmtIndex;
        pFmt->mnType = nFmtType;
    }
}

ScExternalRefCache::TokenRef ScExternalRefManager::getSingleRefTokenFromSrcDoc(
    sal_uInt16 nFileId, ScDocument& rSrcDoc, const ScAddress& rPos,
    ScExternalRefCache::CellFormat* pFmt)
{
    // Get the cell from src doc, and convert it into a token.
    ScRefCellValue aCell(rSrcDoc, rPos);
    ScExternalRefCache::TokenRef pToken(convertToToken(mrDoc, rSrcDoc, aCell));

    if (!pToken)
    {
        // Generate an error for unresolvable cells.
        pToken.reset( new FormulaErrorToken( FormulaError::NoValue));
    }

    // Get number format information.
    sal_uInt32 nFmtIndex = rSrcDoc.GetNumberFormat(rPos.Col(), rPos.Row(), rPos.Tab());
    nFmtIndex = getMappedNumberFormat(nFileId, nFmtIndex, rSrcDoc);
    fillCellFormat(nFmtIndex, pFmt);
    return pToken;
}

ScExternalRefCache::TokenArrayRef ScExternalRefManager::getDoubleRefTokensFromSrcDoc(
    const ScDocument& rSrcDoc, const OUString& rTabName, ScRange& rRange,
    vector<ScExternalRefCache::SingleRangeData>& rCacheData)
{
    ScExternalRefCache::TokenArrayRef pArray;
    SCTAB nTab1;

    if (!rSrcDoc.GetTable(rTabName, nTab1))
    {
        // specified table name doesn't exist in the source document.
        pArray = std::make_shared<ScTokenArray>(rSrcDoc);
        pArray->AddToken(FormulaErrorToken(FormulaError::NoRef));
        return pArray;
    }

    ScRange aRange(rRange);
    aRange.PutInOrder();
    SCTAB nTabSpan = aRange.aEnd.Tab() - aRange.aStart.Tab();

    vector<ScExternalRefCache::SingleRangeData> aCacheData;
    aCacheData.reserve(nTabSpan+1);
    aCacheData.emplace_back();
    aCacheData.back().maTableName = ScGlobal::getCharClass().uppercase(rTabName);

    for (SCTAB i = 1; i < nTabSpan + 1; ++i)
    {
        OUString aTabName;
        if (!rSrcDoc.GetName(nTab1 + 1, aTabName))
            // source document doesn't have any table by the specified name.
            break;

        aCacheData.emplace_back();
        aCacheData.back().maTableName = ScGlobal::getCharClass().uppercase(aTabName);
    }

    aRange.aStart.SetTab(nTab1);
    aRange.aEnd.SetTab(nTab1 + nTabSpan);

    pArray = convertToTokenArray(mrDoc, rSrcDoc, aRange, aCacheData);
    rRange = aRange;
    rCacheData.swap(aCacheData);
    return pArray;
}

ScExternalRefCache::TokenArrayRef ScExternalRefManager::getRangeNameTokensFromSrcDoc(
    sal_uInt16 nFileId, const ScDocument& rSrcDoc, OUString& rName)
{
    ScRangeName* pExtNames = rSrcDoc.GetRangeName();
    OUString aUpperName = ScGlobal::getCharClass().uppercase(rName);
    const ScRangeData* pRangeData = pExtNames->findByUpperName(aUpperName);
    if (!pRangeData)
        return ScExternalRefCache::TokenArrayRef();

    // Parse all tokens in this external range data, and replace each absolute
    // reference token with an external reference token, and cache them.  Also
    // register the source document with the link manager if it's a new
    // source.

    ScExternalRefCache::TokenArrayRef pNew = std::make_shared<ScTokenArray>(rSrcDoc);

    ScTokenArray aCode(*pRangeData->GetCode());
    FormulaTokenArrayPlainIterator aIter(aCode);
    for (const FormulaToken* pToken = aIter.First(); pToken; pToken = aIter.Next())
    {
        bool bTokenAdded = false;
        switch (pToken->GetType())
        {
            case svSingleRef:
            {
                const ScSingleRefData& rRef = *pToken->GetSingleRef();
                OUString aTabName;
                rSrcDoc.GetName(rRef.Tab(), aTabName);
                ScExternalSingleRefToken aNewToken(nFileId, svl::SharedString( aTabName),   // string not interned
                        *pToken->GetSingleRef());
                pNew->AddToken(aNewToken);
                bTokenAdded = true;
            }
            break;
            case svDoubleRef:
            {
                const ScSingleRefData& rRef = *pToken->GetSingleRef();
                OUString aTabName;
                rSrcDoc.GetName(rRef.Tab(), aTabName);
                ScExternalDoubleRefToken aNewToken(nFileId, svl::SharedString( aTabName),   // string not interned
                        *pToken->GetDoubleRef());
                pNew->AddToken(aNewToken);
                bTokenAdded = true;
            }
            break;
            default:
                ;   // nothing
        }

        if (!bTokenAdded)
            pNew->AddToken(*pToken);
    }

    rName = pRangeData->GetName(); // Get the correctly-cased name.
    return pNew;
}

ScDocument* ScExternalRefManager::getInMemorySrcDocument(sal_uInt16 nFileId)
{
    const OUString* pFileName = getExternalFileName(nFileId);
    if (!pFileName)
        return nullptr;

    // Do not load document until it was allowed.
    if (!isLinkUpdateAllowedInDoc(mrDoc))
        return nullptr;

    ScDocument* pSrcDoc = nullptr;
    ScDocShell* pShell = static_cast<ScDocShell*>(SfxObjectShell::GetFirst(checkSfxObjectShell<ScDocShell>, false));
    while (pShell)
    {
        SfxMedium* pMedium = pShell->GetMedium();
        if (pMedium && !pMedium->GetName().isEmpty())
        {
            // TODO: We should make the case sensitivity platform dependent.
            if (pFileName->equalsIgnoreAsciiCase(pMedium->GetName()))
            {
                // Found !
                pSrcDoc = &pShell->GetDocument();
                break;
            }
        }
        else
        {
            // handle unsaved documents here
            OUString aName = pShell->GetName();
            if (pFileName->equalsIgnoreAsciiCase(aName))
            {
                // Found !
                SrcShell aSrcDoc;
                aSrcDoc.maShell = pShell;
                maUnsavedDocShells.emplace(nFileId, aSrcDoc);
                StartListening(*pShell);
                pSrcDoc = &pShell->GetDocument();
                break;
            }
        }
        pShell = static_cast<ScDocShell*>(SfxObjectShell::GetNext(*pShell, checkSfxObjectShell<ScDocShell>, false));
    }

    initDocInCache(maRefCache, pSrcDoc, nFileId);
    return pSrcDoc;
}

ScDocument* ScExternalRefManager::getSrcDocument(sal_uInt16 nFileId)
{
    if (!mrDoc.IsExecuteLinkEnabled())
        return nullptr;

    DocShellMap::iterator itrEnd = maDocShells.end();
    DocShellMap::iterator itr = maDocShells.find(nFileId);

    if (itr != itrEnd)
    {
        // document already loaded.

        SfxObjectShell* p = itr->second.maShell.get();
        itr->second.maLastAccess = tools::Time( tools::Time::SYSTEM );
        return &static_cast<ScDocShell*>(p)->GetDocument();
    }

    itrEnd = maUnsavedDocShells.end();
    itr = maUnsavedDocShells.find(nFileId);
    if (itr != itrEnd)
    {
        //document is unsaved document

        SfxObjectShell* p = itr->second.maShell.get();
        itr->second.maLastAccess = tools::Time( tools::Time::SYSTEM );
        return &static_cast<ScDocShell*>(p)->GetDocument();
    }

    const OUString* pFile = getExternalFileName(nFileId);
    if (!pFile)
        // no file name associated with this ID.
        return nullptr;

    SrcShell aSrcDoc;
    try
    {
        OUString aFilter;
        aSrcDoc.maShell = loadSrcDocument(nFileId, aFilter);
    }
    catch (const css::uno::Exception&)
    {
    }
    if (!aSrcDoc.maShell.is())
    {
        // source document could not be loaded.
        return nullptr;
    }

    return &cacheNewDocShell(nFileId, aSrcDoc);
}

SfxObjectShellRef ScExternalRefManager::loadSrcDocument(sal_uInt16 nFileId, OUString& rFilter)
{
    // Do not load document until it was allowed.
    if (!isLinkUpdateAllowedInDoc(mrDoc))
        return nullptr;

    const SrcFileData* pFileData = getExternalFileData(nFileId);
    if (!pFileData)
        return nullptr;

    // Always load the document by using the path created from the relative
    // path.  If the referenced document is not there, simply exit.  The
    // original file name should be used only when the relative path is not
    // given.
    OUString aFile = pFileData->maFileName;
    maybeCreateRealFileName(nFileId);
    if (!pFileData->maRealFileName.isEmpty())
        aFile = pFileData->maRealFileName;

    if (!isFileLoadable(aFile))
        return nullptr;

    INetURLObject aURLObject(aFile);
    const OUString sHost = aURLObject.GetHost();
    if (HostFilter::isForbidden(sHost))
    {
        SAL_WARN( "sc.ui", "ScExternalRefManager::loadSrcDocument: blocked access to external file: \"" << aFile << "\"");
        return nullptr;
    }

    OUString aOptions = pFileData->maFilterOptions;
    if ( !pFileData->maFilterName.isEmpty() )
        rFilter = pFileData->maFilterName;      // don't overwrite stored filter with guessed filter
    else
        ScDocumentLoader::GetFilterName(aFile, rFilter, aOptions, true, false);
    std::shared_ptr<const SfxFilter> pFilter = ScDocShell::Factory().GetFilterContainer()->GetFilter4FilterName(rFilter);

    if (pFileData->maRelativeName.isEmpty())
    {
        // Generate a relative file path.
        INetURLObject aBaseURL(getOwnDocumentName());
        aBaseURL.insertName(u"content.xml");

        OUString aStr = URIHelper::simpleNormalizedMakeRelative(
            aBaseURL.GetMainURL(INetURLObject::DecodeMechanism::NONE), aFile);

        setRelativeFileName(nFileId, aStr);
    }

    std::unique_ptr<SfxItemSet> pSet(new SfxAllItemSet(SfxGetpApp()->GetPool()));
    if (!aOptions.isEmpty())
        pSet->Put(SfxStringItem(SID_FILE_FILTEROPTIONS, aOptions));

    // make medium hidden to prevent assertion from progress bar
    pSet->Put( SfxBoolItem(SID_HIDDEN, true) );

    // If the current document is allowed to execute macros then the referenced
    // document may execute macros according to the security configuration.
    // Similar for UpdateDocMode to update links, just that if we reach here
    // the user already allowed updates and intermediate documents are expected
    // to update as well. When loading the document ScDocShell::Load() will
    // check through ScDocShell::GetLinkUpdateModeState() if its location is
    // trusted.
    ScDocShell* pShell = mrDoc.GetDocumentShell();
    if (pShell)
    {
        SfxMedium* pMedium = pShell->GetMedium();
        if (pMedium)
        {
            const SfxUInt16Item* pItem = pMedium->GetItemSet().GetItemIfSet( SID_MACROEXECMODE, false );
            if (pItem &&
                    pItem->GetValue() != css::document::MacroExecMode::NEVER_EXECUTE)
                pSet->Put( SfxUInt16Item( SID_MACROEXECMODE, css::document::MacroExecMode::USE_CONFIG));
        }

        pSet->Put( SfxUInt16Item( SID_UPDATEDOCMODE, css::document::UpdateDocMode::FULL_UPDATE));
    }

    unique_ptr<SfxMedium> pMedium(new SfxMedium(aFile, StreamMode::STD_READ,
                                                std::move(pFilter), std::move(pSet)));
    if (pMedium->GetErrorIgnoreWarning() != ERRCODE_NONE)
        return nullptr;

    // To load encrypted documents with password, user interaction needs to be enabled.
    pMedium->UseInteractionHandler(mbUserInteractionEnabled);

    rtl::Reference<ScDocShell> pNewShell = new ScDocShell(SfxModelFlags::EXTERNAL_LINK);

    // increment the recursive link count of the source document.
    ScExtDocOptions* pExtOpt = mrDoc.GetExtDocOptions();
    sal_uInt32 nLinkCount = pExtOpt ? pExtOpt->GetDocSettings().mnLinkCnt : 0;
    ScDocument& rSrcDoc = pNewShell->GetDocument();
    rSrcDoc.EnableExecuteLink(false); // to prevent circular access of external references.
    rSrcDoc.EnableUndo(false);
    rSrcDoc.LockAdjustHeight();
    rSrcDoc.EnableUserInteraction(false);

    ScExtDocOptions* pExtOptNew = rSrcDoc.GetExtDocOptions();
    if (!pExtOptNew)
    {
        rSrcDoc.SetExtDocOptions(std::make_unique<ScExtDocOptions>());
        pExtOptNew = rSrcDoc.GetExtDocOptions();
    }
    pExtOptNew->GetDocSettings().mnLinkCnt = nLinkCount + 1;

    if (!pNewShell->DoLoad(pMedium.release()))
    {
        pNewShell->DoClose();
        pNewShell.clear();
        return pNewShell;
    }

    // with UseInteractionHandler, options may be set by dialog during DoLoad
    OUString aNew = ScDocumentLoader::GetOptions(*pNewShell->GetMedium());
    if (!aNew.isEmpty() && aNew != aOptions)
        aOptions = aNew;
    setFilterData(nFileId, rFilter, aOptions);    // update the filter data, including the new options

    return pNewShell;
}

ScDocument& ScExternalRefManager::cacheNewDocShell( sal_uInt16 nFileId, SrcShell& rSrcShell )
{
    if (mbDocTimerEnabled && maDocShells.empty())
        // If this is the first source document insertion, start up the timer.
        maSrcDocTimer.Start();

    maDocShells.emplace(nFileId, rSrcShell);
    SfxObjectShell& rShell = *rSrcShell.maShell;
    ScDocument& rSrcDoc = static_cast<ScDocShell&>(rShell).GetDocument();
    initDocInCache(maRefCache, &rSrcDoc, nFileId);
    return rSrcDoc;
}

bool ScExternalRefManager::isFileLoadable(const OUString& rFile) const
{
    if (rFile.isEmpty())
        return false;

    if (isOwnDocument(rFile))
        return false;
    OUString aPhysical;
    if (osl::FileBase::getSystemPathFromFileURL(rFile, aPhysical)
        == osl::FileBase::E_None)
    {
        // #i114504# try IsFolder/Exists only for file URLs

        if (utl::UCBContentHelper::IsFolder(rFile))
            return false;

        return utl::UCBContentHelper::Exists(rFile);
    }
    else
        return true;    // for http and others, Exists doesn't work, but the URL can still be opened
}

void ScExternalRefManager::maybeLinkExternalFile( sal_uInt16 nFileId, bool bDeferFilterDetection )
{
    if (maLinkedDocs.count(nFileId))
        // file already linked, or the link has been broken.
        return;

    // Source document not linked yet.  Link it now.
    const OUString* pFileName = getExternalFileName(nFileId);
    if (!pFileName)
        return;

    OUString aFilter, aOptions;
    const SrcFileData* pFileData = getExternalFileData(nFileId);
    if (pFileData)
    {
        aFilter = pFileData->maFilterName;
        aOptions = pFileData->maFilterOptions;
    }

    // Filter detection may access external links; defer it until we are allowed.
    if (!bDeferFilterDetection)
        bDeferFilterDetection = !isLinkUpdateAllowedInDoc(mrDoc);

    // If a filter was already set (for example, loading the cached table),
    // don't call GetFilterName which has to access the source file.
    // If filter detection is deferred, the next successful loadSrcDocument()
    // will update SrcFileData filter name.
    if (aFilter.isEmpty() && !bDeferFilterDetection)
        ScDocumentLoader::GetFilterName(*pFileName, aFilter, aOptions, true, false);
    sfx2::LinkManager* pLinkMgr = mrDoc.GetLinkManager();
    if (!pLinkMgr)
    {
        SAL_WARN( "sc.ui", "ScExternalRefManager::maybeLinkExternalFile: pLinkMgr==NULL");
        return;
    }
    ScExternalRefLink* pLink = new ScExternalRefLink(mrDoc, nFileId);
    OSL_ENSURE(pFileName, "ScExternalRefManager::maybeLinkExternalFile: file name pointer is NULL");
    pLinkMgr->InsertFileLink(*pLink, sfx2::SvBaseLinkObjectType::ClientFile, *pFileName,
            (aFilter.isEmpty() && bDeferFilterDetection ? nullptr : &aFilter));

    pLink->SetDoRefresh(false);
    pLink->Update();
    pLink->SetDoRefresh(true);

    maLinkedDocs.emplace(nFileId, true);
}

void ScExternalRefManager::addFilesToLinkManager()
{
    if (maSrcFiles.empty())
        return;

    SAL_WARN_IF( maSrcFiles.size() >= SAL_MAX_UINT16,
            "sc.ui", "ScExternalRefManager::addFilesToLinkManager: files overflow");
    const sal_uInt16 nSize = static_cast<sal_uInt16>( std::min<size_t>( maSrcFiles.size(), SAL_MAX_UINT16));
    for (sal_uInt16 nFileId = 0; nFileId < nSize; ++nFileId)
        maybeLinkExternalFile( nFileId, true);
}

void ScExternalRefManager::SrcFileData::maybeCreateRealFileName(std::u16string_view rOwnDocName)
{
    if (maRelativeName.isEmpty())
        // No relative path given.  Nothing to do.
        return;

    if (!maRealFileName.isEmpty())
        // Real file name already created.  Nothing to do.
        return;

    // Formulate the absolute file path from the relative path.
    const OUString& rRelPath = maRelativeName;
    INetURLObject aBaseURL(rOwnDocName);
    aBaseURL.insertName(u"content.xml");
    bool bWasAbs = false;
    maRealFileName = aBaseURL.smartRel2Abs(rRelPath, bWasAbs).GetMainURL(INetURLObject::DecodeMechanism::NONE);
}

void ScExternalRefManager::maybeCreateRealFileName(sal_uInt16 nFileId)
{
    if (nFileId >= maSrcFiles.size())
        return;

    maSrcFiles[nFileId].maybeCreateRealFileName(getOwnDocumentName());
}

OUString ScExternalRefManager::getOwnDocumentName() const
{
    if (comphelper::IsFuzzing())
        return u"file:///tmp/document"_ustr;

    ScDocShell* pShell = mrDoc.GetDocumentShell();
    if (!pShell)
        // This should not happen!
        return OUString();

    SfxMedium* pMed = pShell->GetMedium();
    if (!pMed)
        return OUString();

    return pMed->GetName();
}

bool ScExternalRefManager::isOwnDocument(std::u16string_view rFile) const
{
    return getOwnDocumentName() == rFile;
}

void ScExternalRefManager::convertToAbsName(OUString& rFile) const
{
    // unsaved documents have no AbsName
    ScDocShell* pShell = static_cast<ScDocShell*>(SfxObjectShell::GetFirst(checkSfxObjectShell<ScDocShell>, false));
    while (pShell)
    {
        if (rFile == pShell->GetName())
            return;

        pShell = static_cast<ScDocShell*>(SfxObjectShell::GetNext(*pShell, checkSfxObjectShell<ScDocShell>, false));
    }

    ScDocShell* pDocShell = mrDoc.GetDocumentShell();
    rFile = ScGlobal::GetAbsDocName(rFile, pDocShell);
}

sal_uInt16 ScExternalRefManager::getExternalFileId(const OUString& rFile)
{
    vector<SrcFileData>::const_iterator itrBeg = maSrcFiles.begin(), itrEnd = maSrcFiles.end();
    vector<SrcFileData>::const_iterator itr = find_if(itrBeg, itrEnd, FindSrcFileByName(rFile));
    if (itr != itrEnd)
    {
        size_t nId = distance(itrBeg, itr);
        return static_cast<sal_uInt16>(nId);
    }

    SrcFileData aData;
    aData.maFileName = rFile;
    maSrcFiles.push_back(aData);
    return static_cast<sal_uInt16>(maSrcFiles.size() - 1);
}

const OUString* ScExternalRefManager::getExternalFileName(sal_uInt16 nFileId, bool bForceOriginal)
{
    if (nFileId >= maSrcFiles.size())
        return nullptr;

    if (bForceOriginal)
        return &maSrcFiles[nFileId].maFileName;

    maybeCreateRealFileName(nFileId);

    if (!maSrcFiles[nFileId].maRealFileName.isEmpty())
        return &maSrcFiles[nFileId].maRealFileName;

    return &maSrcFiles[nFileId].maFileName;
}

sal_uInt16 ScExternalRefManager::convertFileIdToUsedFileId(sal_uInt16 nFileId)
{
    if (!mbSkipUnusedFileIds)
        return nFileId;
    else
        return maConvertFileIdToUsedFileId[nFileId];
}

void ScExternalRefManager::setSkipUnusedFileIds(std::vector<sal_uInt16>& rExternFileIds)
{
    mbSkipUnusedFileIds = true;
    maConvertFileIdToUsedFileId.resize(maSrcFiles.size());
    std::fill(maConvertFileIdToUsedFileId.begin(), maConvertFileIdToUsedFileId.end(), 0);
    int nUsedCount = 0;
    for (auto nEntry : rExternFileIds)
    {
        maConvertFileIdToUsedFileId[nEntry] = nUsedCount++;
    }
}

void ScExternalRefManager::disableSkipUnusedFileIds()
{
    mbSkipUnusedFileIds = false;
}

std::vector<OUString> ScExternalRefManager::getAllCachedExternalFileNames() const
{
    std::vector<OUString> aNames;
    aNames.reserve(maSrcFiles.size());
    for (const SrcFileData& rData : maSrcFiles)
    {
        aNames.push_back(rData.maFileName);
    }

    return aNames;
}

bool ScExternalRefManager::hasExternalFile(sal_uInt16 nFileId) const
{
    return nFileId < maSrcFiles.size();
}

bool ScExternalRefManager::hasExternalFile(const OUString& rFile) const
{
    return ::std::any_of(maSrcFiles.begin(), maSrcFiles.end(), FindSrcFileByName(rFile));
}

const ScExternalRefManager::SrcFileData* ScExternalRefManager::getExternalFileData(sal_uInt16 nFileId) const
{
    if (nFileId >= maSrcFiles.size())
        return nullptr;

    return &maSrcFiles[nFileId];
}

const OUString* ScExternalRefManager::getRealTableName(sal_uInt16 nFileId, const OUString& rTabName) const
{
    return maRefCache.getRealTableName(nFileId, rTabName);
}

const OUString* ScExternalRefManager::getRealRangeName(sal_uInt16 nFileId, const OUString& rRangeName) const
{
    return maRefCache.getRealRangeName(nFileId, rRangeName);
}

template<typename MapContainer>
static void lcl_removeByFileId(sal_uInt16 nFileId, MapContainer& rMap)
{
    typename MapContainer::iterator itr = rMap.find(nFileId);
    if (itr != rMap.end())
    {
        // Close this document shell.
        itr->second.maShell->DoClose();
        rMap.erase(itr);
    }
}

void ScExternalRefManager::clearCache(sal_uInt16 nFileId)
{
    maRefCache.clearCache(nFileId);
}

namespace {

class RefCacheFiller : public sc::ColumnSpanSet::ColumnAction
{
    svl::SharedStringPool& mrStrPool;

    ScExternalRefCache& mrRefCache;
    ScExternalRefCache::TableTypeRef mpRefTab;
    sal_uInt16 mnFileId;
    ScColumn* mpCurCol;
    sc::ColumnBlockConstPosition maBlockPos;

public:
    RefCacheFiller( svl::SharedStringPool& rStrPool, ScExternalRefCache& rRefCache, sal_uInt16 nFileId ) :
        mrStrPool(rStrPool), mrRefCache(rRefCache), mnFileId(nFileId), mpCurCol(nullptr) {}

    virtual void startColumn( ScColumn* pCol ) override
    {
        mpCurCol = pCol;
        if (!mpCurCol)
            return;

        mpCurCol->InitBlockPosition(maBlockPos);
        mpRefTab = mrRefCache.getCacheTable(mnFileId, mpCurCol->GetTab());
    }

    virtual void execute( SCROW nRow1, SCROW nRow2, bool bVal ) override
    {
        if (!mpCurCol || !bVal)
            return;

        if (!mpRefTab)
            return;

        for (SCROW nRow = nRow1; nRow <= nRow2; ++nRow)
        {
            ScExternalRefCache::TokenRef pTok;
            ScRefCellValue aCell = mpCurCol->GetCellValue(maBlockPos, nRow);
            switch (aCell.getType())
            {
                case CELLTYPE_STRING:
                case CELLTYPE_EDIT:
                {
                    OUString aStr = aCell.getString(mpCurCol->GetDoc());
                    svl::SharedString aSS = mrStrPool.intern(aStr);
                    pTok.reset(new formula::FormulaStringToken(std::move(aSS)));
                }
                break;
                case CELLTYPE_VALUE:
                    pTok.reset(new formula::FormulaDoubleToken(aCell.getDouble()));
                break;
                case CELLTYPE_FORMULA:
                {
                    sc::FormulaResultValue aRes = aCell.getFormula()->GetResult();
                    switch (aRes.meType)
                    {
                        case sc::FormulaResultValue::Value:
                            pTok.reset(new formula::FormulaDoubleToken(aRes.mfValue));
                        break;
                        case sc::FormulaResultValue::String:
                        {
                            // Re-intern the string to the host document pool.
                            svl::SharedString aInterned = mrStrPool.intern(aRes.maString.getString());
                            pTok.reset(new formula::FormulaStringToken(std::move(aInterned)));
                        }
                        break;
                        case sc::FormulaResultValue::Error:
                        case sc::FormulaResultValue::Invalid:
                        default:
                            pTok.reset(new FormulaErrorToken(FormulaError::NoValue));
                    }
                }
                break;
                default:
                    pTok.reset(new FormulaErrorToken(FormulaError::NoValue));
            }

            if (pTok)
            {
                // Cache this cell.
                mpRefTab->setCell(mpCurCol->GetCol(), nRow, pTok, mpCurCol->GetNumberFormat(mpCurCol->GetDoc().GetNonThreadedContext(), nRow));
                mpRefTab->setCachedCell(mpCurCol->GetCol(), nRow);
            }
        }
    };
};

}

bool ScExternalRefManager::refreshSrcDocument(sal_uInt16 nFileId)
{
    SfxObjectShellRef xDocShell;
    try
    {
        OUString aFilter;
        xDocShell = loadSrcDocument(nFileId, aFilter);
    }
    catch ( const css::uno::Exception& ) {}

    if (!xDocShell.is())
        // Failed to load the document.  Bail out.
        return false;

    ScDocShell& rDocSh = static_cast<ScDocShell&>(*xDocShell);
    ScDocument& rSrcDoc = rDocSh.GetDocument();

    sc::ColumnSpanSet aCachedArea;
    maRefCache.getAllCachedDataSpans(rSrcDoc, nFileId, aCachedArea);

    // Clear the existing cache, and refill it.  Make sure we keep the
    // existing cache table instances here.
    maRefCache.clearCacheTables(nFileId);
    RefCacheFiller aAction(mrDoc.GetSharedStringPool(), maRefCache, nFileId);
    aCachedArea.executeColumnAction(rSrcDoc, aAction);

    DocShellMap::iterator it = maDocShells.find(nFileId);
    if (it != maDocShells.end())
    {
        it->second.maShell->DoClose();
        it->second.maShell = std::move(xDocShell);
        it->second.maLastAccess = tools::Time(tools::Time::SYSTEM);
    }
    else
    {
        SrcShell aSrcDoc;
        aSrcDoc.maShell = std::move(xDocShell);
        aSrcDoc.maLastAccess = tools::Time(tools::Time::SYSTEM);
        cacheNewDocShell(nFileId, aSrcDoc);
    }

    // Update all cells containing names from this source document.
    refreshAllRefCells(nFileId);

    notifyAllLinkListeners(nFileId, LINK_MODIFIED);

    return true;
}

void ScExternalRefManager::breakLink(sal_uInt16 nFileId)
{
    // Turn all formula cells referencing this external document into static
    // cells.
    RefCellMap::iterator itrRefs = maRefCells.find(nFileId);
    if (itrRefs != maRefCells.end())
    {
        // Make a copy because removing the formula cells below will modify
        // the original container.
        RefCellSet aSet = itrRefs->second;
        for_each(aSet.begin(), aSet.end(), ConvertFormulaToStatic(&mrDoc));
        maRefCells.erase(nFileId);
    }

    // Remove all named ranges that reference this document.

    // Global named ranges.
    ScRangeName* pRanges = mrDoc.GetRangeName();
    if (pRanges)
        removeRangeNamesBySrcDoc(*pRanges, nFileId);

    // Sheet-local named ranges.
    for (SCTAB i = 0, n = mrDoc.GetTableCount(); i < n; ++i)
    {
        pRanges = mrDoc.GetRangeName(i);
        if (pRanges)
            removeRangeNamesBySrcDoc(*pRanges, nFileId);
    }

    clearCache(nFileId);
    lcl_removeByFileId(nFileId, maDocShells);

    if (maDocShells.empty())
        maSrcDocTimer.Stop();

    LinkedDocMap::iterator itr = maLinkedDocs.find(nFileId);
    if (itr != maLinkedDocs.end())
        itr->second = false;

    notifyAllLinkListeners(nFileId, LINK_BROKEN);
}

void ScExternalRefManager::switchSrcFile(sal_uInt16 nFileId, const OUString& rNewFile, const OUString& rNewFilter)
{
    maSrcFiles[nFileId].maFileName = rNewFile;
    maSrcFiles[nFileId].maRelativeName.clear();
    maSrcFiles[nFileId].maRealFileName.clear();
    if (maSrcFiles[nFileId].maFilterName != rNewFilter)
    {
        // Filter type has changed.
        maSrcFiles[nFileId].maFilterName = rNewFilter;
        maSrcFiles[nFileId].maFilterOptions.clear();
    }
    refreshSrcDocument(nFileId);
}

void ScExternalRefManager::setRelativeFileName(sal_uInt16 nFileId, const OUString& rRelUrl)
{
    if (nFileId >= maSrcFiles.size())
        return;
    maSrcFiles[nFileId].maRelativeName = rRelUrl;
}

void ScExternalRefManager::setFilterData(sal_uInt16 nFileId, const OUString& rFilterName, const OUString& rOptions)
{
    if (nFileId >= maSrcFiles.size())
        return;
    maSrcFiles[nFileId].maFilterName = rFilterName;
    maSrcFiles[nFileId].maFilterOptions = rOptions;
}

void ScExternalRefManager::clear()
{
    for (auto& rEntry : maLinkListeners)
    {
        for (auto& it : rEntry.second)
        {
            it->notify(0, OH_NO_WE_ARE_GOING_TO_DIE);
        }
    }

    for (auto& rEntry : maDocShells)
        rEntry.second.maShell->DoClose();

    maDocShells.clear();
    maSrcDocTimer.Stop();
}

bool ScExternalRefManager::hasExternalData() const
{
    return !maSrcFiles.empty();
}

void ScExternalRefManager::resetSrcFileData(const OUString& rBaseFileUrl)
{
    for (auto& rSrcFile : maSrcFiles)
    {
        // Re-generate relative file name from the absolute file name.
        OUString aAbsName = rSrcFile.maRealFileName;
        if (aAbsName.isEmpty())
            aAbsName = rSrcFile.maFileName;

        rSrcFile.maRelativeName = URIHelper::simpleNormalizedMakeRelative(
            rBaseFileUrl, aAbsName);
    }
}

void ScExternalRefManager::updateAbsAfterLoad()
{
    OUString aOwn( getOwnDocumentName() );
    for (auto& rSrcFile : maSrcFiles)
    {
        // update maFileName to the real file name,
        // to be called when the original name is no longer needed (after CompileXML)

        rSrcFile.maybeCreateRealFileName( aOwn );
        OUString aReal = rSrcFile.maRealFileName;
        if (!aReal.isEmpty())
            rSrcFile.maFileName = aReal;
    }
}

void ScExternalRefManager::removeRefCell(ScFormulaCell* pCell)
{
    for_each(maRefCells.begin(), maRefCells.end(), RemoveFormulaCell(pCell));
}

void ScExternalRefManager::addLinkListener(sal_uInt16 nFileId, LinkListener* pListener)
{
    LinkListenerMap::iterator itr = maLinkListeners.find(nFileId);
    if (itr == maLinkListeners.end())
    {
        pair<LinkListenerMap::iterator, bool> r = maLinkListeners.emplace(
            nFileId, LinkListeners());
        if (!r.second)
        {
            OSL_FAIL("insertion of new link listener list failed");
            return;
        }

        itr = r.first;
    }

    LinkListeners& rList = itr->second;
    rList.insert(pListener);
}

void ScExternalRefManager::removeLinkListener(sal_uInt16 nFileId, LinkListener* pListener)
{
    LinkListenerMap::iterator itr = maLinkListeners.find(nFileId);
    if (itr == maLinkListeners.end())
        // no listeners for a specified file.
        return;

    LinkListeners& rList = itr->second;
    rList.erase(pListener);

    if (rList.empty())
        // No more listeners for this file.  Remove its entry.
        maLinkListeners.erase(itr);
}

void ScExternalRefManager::removeLinkListener(LinkListener* pListener)
{
    for (auto& rEntry : maLinkListeners)
        rEntry.second.erase(pListener);
}

void ScExternalRefManager::notifyAllLinkListeners(sal_uInt16 nFileId, LinkUpdateType eType)
{
    LinkListenerMap::iterator itr = maLinkListeners.find(nFileId);
    if (itr == maLinkListeners.end())
        // no listeners for a specified file.
        return;

    LinkListeners& rList = itr->second;
    for_each(rList.begin(), rList.end(), NotifyLinkListener(nFileId, eType));
}

void ScExternalRefManager::purgeStaleSrcDocument(sal_Int32 nTimeOut)
{
    // To avoid potentially freezing Calc, we close one stale document at a time.
    DocShellMap::iterator itr = std::find_if(maDocShells.begin(), maDocShells.end(),
        [nTimeOut](const DocShellMap::value_type& rEntry) {
            // in 100th of a second.
            sal_Int32 nSinceLastAccess = (tools::Time( tools::Time::SYSTEM ) - rEntry.second.maLastAccess).GetTime();
            return nSinceLastAccess >= nTimeOut;
        });
    if (itr != maDocShells.end())
    {
        // Timed out.  Let's close this.
        itr->second.maShell->DoClose();
        maDocShells.erase(itr);
    }

    if (maDocShells.empty())
        maSrcDocTimer.Stop();
}

sal_uInt32 ScExternalRefManager::getMappedNumberFormat(sal_uInt16 nFileId, sal_uInt32 nNumFmt, const ScDocument& rSrcDoc)
{
    NumFmtMap::iterator itr = maNumFormatMap.find(nFileId);
    if (itr == maNumFormatMap.end())
    {
        // Number formatter map is not initialized for this external document.
        pair<NumFmtMap::iterator, bool> r = maNumFormatMap.emplace(
            nFileId, SvNumberFormatterMergeMap());

        if (!r.second)
            // insertion failed.
            return nNumFmt;

        itr = r.first;
        mrDoc.GetFormatTable()->MergeFormatter(*rSrcDoc.GetFormatTable());
        SvNumberFormatterMergeMap aMap = mrDoc.GetFormatTable()->ConvertMergeTableToMap();
        itr->second.swap(aMap);
    }
    const SvNumberFormatterMergeMap& rMap = itr->second;
    SvNumberFormatterMergeMap::const_iterator itrNumFmt = rMap.find(nNumFmt);
    if (itrNumFmt != rMap.end())
        // mapped value found.
        return itrNumFmt->second;

    return nNumFmt;
}

void ScExternalRefManager::transformUnsavedRefToSavedRef( SfxObjectShell* pShell )
{
    DocShellMap::iterator itr = maUnsavedDocShells.begin();
    while( itr != maUnsavedDocShells.end() )
    {
        if ( itr->second.maShell.get() == pShell )
        {
            // found that the shell is marked as unsaved
            OUString aFileURL = pShell->GetMedium()->GetURLObject().GetMainURL(INetURLObject::DecodeMechanism::ToIUri);
            switchSrcFile(itr->first, aFileURL, OUString());
            EndListening(*pShell);
            itr = maUnsavedDocShells.erase(itr);
        }
        else
            ++itr;
    }
}

void ScExternalRefManager::Notify( SfxBroadcaster&, const SfxHint& rHint )
{
    if (rHint.GetId() != SfxHintId::ThisIsAnSfxEventHint)
        return;

    switch (static_cast<const SfxEventHint&>(rHint).GetEventId())
    {
        case SfxEventHintId::PrepareCloseDoc:
            {
                std::unique_ptr<weld::MessageDialog> xWarn(Application::CreateMessageDialog(ScDocShell::GetActiveDialogParent(),
                                                           VclMessageType::Warning, VclButtonsType::Ok,
                                                           ScResId(STR_CLOSE_WITH_UNSAVED_REFS)));
                xWarn->run();
            }
            break;
        case SfxEventHintId::SaveDocDone:
        case SfxEventHintId::SaveAsDocDone:
            {
                rtl::Reference<SfxObjectShell> pObjShell = static_cast<const SfxEventHint&>( rHint ).GetObjShell();
                transformUnsavedRefToSavedRef(pObjShell.get());
            }
            break;
        default:
            break;
    }
}

IMPL_LINK(ScExternalRefManager, TimeOutHdl, Timer*, pTimer, void)
{
    if (pTimer == &maSrcDocTimer)
        purgeStaleSrcDocument(SRCDOC_LIFE_SPAN);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
