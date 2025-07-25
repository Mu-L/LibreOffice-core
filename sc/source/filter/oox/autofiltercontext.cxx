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

#include <autofiltercontext.hxx>
#include <biffhelper.hxx>

#include <autofilterbuffer.hxx>
#include <oox/token/namespaces.hxx>

namespace oox::xls {

using ::oox::core::ContextHandlerRef;

FilterSettingsContext::FilterSettingsContext( WorksheetContextBase& rParent, FilterSettingsBase& rFilterSettings ) :
    WorksheetContextBase( rParent ),
    mrFilterSettings( rFilterSettings )
{
}

ContextHandlerRef FilterSettingsContext::onCreateContext( sal_Int32 nElement, const AttributeList& /*rAttribs*/ )
{
    switch( getCurrentElement() )
    {
        case XLS_TOKEN( filters ):
            if( nElement == XLS_TOKEN( filter ) || nElement == XLS_TOKEN( dateGroupItem )) return this;
        break;
        case XLS_TOKEN( customFilters ):
            if( nElement == XLS_TOKEN( customFilter ) ) return this;
        break;
        case XLS_TOKEN( colorFilter ):
            if( nElement == XLS_TOKEN( colorFilter ) ) return this;
        break;
    }
    return nullptr;
}

void FilterSettingsContext::onStartElement( const AttributeList& rAttribs )
{
    mrFilterSettings.importAttribs( getCurrentElement(), rAttribs );
}

ContextHandlerRef FilterSettingsContext::onCreateRecordContext( sal_Int32 nRecId, SequenceInputStream& /*rStrm*/ )
{
    switch( getCurrentElement() )
    {
        case BIFF12_ID_DISCRETEFILTERS:
            if( nRecId == BIFF12_ID_DISCRETEFILTER ) return this;
        break;
        case BIFF12_ID_CUSTOMFILTERS:
            if( nRecId == BIFF12_ID_CUSTOMFILTER ) return this;
        break;
    }
    return nullptr;
}

void FilterSettingsContext::onStartRecord( SequenceInputStream& rStrm )
{
    mrFilterSettings.importRecord( getCurrentElement(), rStrm );
}

FilterColumnContext::FilterColumnContext( WorksheetContextBase& rParent, FilterColumn& rFilterColumn ) :
    WorksheetContextBase( rParent ),
    mrFilterColumn( rFilterColumn )
{
}

ContextHandlerRef FilterColumnContext::onCreateContext( sal_Int32 nElement, const AttributeList& /*rAttribs*/ )
{
    if( getCurrentElement() == XLS_TOKEN( filterColumn ) ) switch( nElement )
    {
        case XLS_TOKEN( filters ):
            return new FilterSettingsContext( *this, mrFilterColumn.createFilterSettings< DiscreteFilter >() );
        case XLS_TOKEN( top10 ):
            return new FilterSettingsContext( *this, mrFilterColumn.createFilterSettings< Top10Filter >() );
        case XLS_TOKEN( customFilters ):
            return new FilterSettingsContext( *this, mrFilterColumn.createFilterSettings< CustomFilter >() );
        case XLS_TOKEN( colorFilter ):
            return new FilterSettingsContext( *this, mrFilterColumn.createFilterSettings< ColorFilter >() );
    }
    return nullptr;
}

void FilterColumnContext::onStartElement( const AttributeList& rAttribs )
{
    mrFilterColumn.importFilterColumn( rAttribs );
}

ContextHandlerRef FilterColumnContext::onCreateRecordContext( sal_Int32 nRecId, SequenceInputStream& /*rStrm*/ )
{
    if( getCurrentElement() == BIFF12_ID_FILTERCOLUMN ) switch( nRecId )
    {
        case BIFF12_ID_DISCRETEFILTERS:
            return new FilterSettingsContext( *this, mrFilterColumn.createFilterSettings< DiscreteFilter >() );
        case BIFF12_ID_TOP10FILTER:
            return new FilterSettingsContext( *this, mrFilterColumn.createFilterSettings< Top10Filter >() );
        case BIFF12_ID_CUSTOMFILTERS:
            return new FilterSettingsContext( *this, mrFilterColumn.createFilterSettings< CustomFilter >() );
    }
    return nullptr;
}

void FilterColumnContext::onStartRecord( SequenceInputStream& rStrm )
{
    mrFilterColumn.importFilterColumn( rStrm );
}

// class SortConditionContext

SortConditionContext::SortConditionContext( WorksheetContextBase& rParent, SortCondition& rSortCondition ) :
    WorksheetContextBase( rParent ),
    mrSortCondition( rSortCondition )
{
}

ContextHandlerRef SortConditionContext::onCreateContext( sal_Int32 , const AttributeList& )
{
    return nullptr;
}

void SortConditionContext::onStartElement( const AttributeList& rAttribs )
{
    mrSortCondition.importSortCondition( rAttribs, getSheetIndex() );
}

ContextHandlerRef SortConditionContext::onCreateRecordContext( sal_Int32 , SequenceInputStream& )
{
    return nullptr;
}

void SortConditionContext::onStartRecord( SequenceInputStream& )
{
}

// class SortStateContext

SortStateContext::SortStateContext( WorksheetContextBase& rParent, AutoFilter& rAutoFilter ) :
    WorksheetContextBase( rParent ),
    mrAutoFilter( rAutoFilter )
{
}

ContextHandlerRef SortStateContext::onCreateContext( sal_Int32 nElement, const AttributeList& /*rAttribs*/ )
{
    if( getCurrentElement() == XLS_TOKEN( sortState ) ) switch( nElement )
    {
        case XLS_TOKEN( sortCondition ):
            return new SortConditionContext( *this, mrAutoFilter.createSortCondition() );
    }
    return nullptr;
}

void SortStateContext::onStartElement( const AttributeList& rAttribs )
{
    mrAutoFilter.importSortState( rAttribs, getSheetIndex() );
}

ContextHandlerRef SortStateContext::onCreateRecordContext( sal_Int32 , SequenceInputStream& )
{
    return nullptr;
}

void SortStateContext::onStartRecord( SequenceInputStream& )
{
}

AutoFilterContext::AutoFilterContext( WorksheetFragmentBase& rFragment, AutoFilter& rAutoFilter ) :
    WorksheetContextBase( rFragment ),
    mrAutoFilter( rAutoFilter )
{
}

ContextHandlerRef AutoFilterContext::onCreateContext( sal_Int32 nElement, const AttributeList& /*rAttribs*/ )
{
    if( getCurrentElement() == XLS_TOKEN( autoFilter ) ) switch( nElement )
    {
        case XLS_TOKEN( sortState ):
            return new SortStateContext( *this, mrAutoFilter );
        case XLS_TOKEN( filterColumn ):
            return new FilterColumnContext( *this, mrAutoFilter.createFilterColumn() );
    }
    return nullptr;
}

void AutoFilterContext::onStartElement( const AttributeList& rAttribs )
{
    mrAutoFilter.importAutoFilter( rAttribs, getSheetIndex() );
}

ContextHandlerRef AutoFilterContext::onCreateRecordContext( sal_Int32 nRecId, SequenceInputStream& /*rStrm*/ )
{
    if( (getCurrentElement() == BIFF12_ID_AUTOFILTER) && (nRecId == BIFF12_ID_FILTERCOLUMN) )
        return new FilterColumnContext( *this, mrAutoFilter.createFilterColumn() );
    return nullptr;
}

void AutoFilterContext::onStartRecord( SequenceInputStream& rStrm )
{
    if( getCurrentElement() == BIFF12_ID_AUTOFILTER )
        mrAutoFilter.importAutoFilter( rStrm, getSheetIndex() );
}

} // namespace oox::xls

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
