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

#include <memory>
#include <cppuhelper/implbase9.hxx>
#include <com/sun/star/sdbcx/XColumnsSupplier.hpp>
#include <com/sun/star/sdbc/XRow.hpp>
#include <com/sun/star/sdbc/XColumnLocate.hpp>
#include <com/sun/star/sdbc/XCloseable.hpp>
#include <com/sun/star/sdbcx/XRowLocate.hpp>
#include <com/sun/star/sdbc/XResultSetMetaDataSupplier.hpp>
#include <com/sun/star/sdbc/XWarningsSupplier.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <cppuhelper/interfacecontainer.h>
#include <connectivity/sqlerror.hxx>
#include <connectivity/CommonTools.hxx>
#include <comphelper/propertystatecontainer.hxx>
#include <comphelper/proparrhlp.hxx>
#include <com/sun/star/sdbc/XRowSet.hpp>
#include <com/sun/star/util/XNumberFormatTypes.hpp>
#include <com/sun/star/container/XNameAccess.hpp>
#include "RowSetRow.hxx"
#include "RowSetCacheIterator.hxx"

#include <functional>

namespace dbaccess
{
    class OEmptyCollection;

    typedef ::cppu::ImplHelper9<                css::sdbcx::XRowLocate,
                                                css::sdbc::XRow,
                                                css::sdbc::XResultSetMetaDataSupplier,
                                                css::sdbc::XWarningsSupplier,
                                                css::sdbc::XColumnLocate,
                                                css::sdbcx::XColumnsSupplier,
                                                css::lang::XServiceInfo,
                                                css::sdbc::XRowSet,
                                                css::sdbc::XCloseable> ORowSetBase_BASE;

    class ORowSetCache;
    class ORowSetDataColumns;
    class ORowSetDataColumn;
    class ORowSetBase : public ORowSetBase_BASE,
                        public ::comphelper::OPropertyStateContainer,
                        public ::comphelper::OPropertyArrayUsageHelper<ORowSetBase> // this class hold the static property info
    {
    protected:
        typedef std::vector<ORowSetDataColumn*>   TDataColumns;
        ::osl::Mutex*                           m_pMutex;           // this is the mutex from the rowset itself
        ::osl::Mutex                            // we need an extra mutex for columns to prevent deadlock when setting new values
                                                // for a row
                                                m_aColumnsMutex;

        css::uno::Any                           m_aBookmark;
        ORowSetCacheIterator                    m_aCurrentRow;      // contains the actual fetched row
        TORowSetOldRowHelperRef                 m_aOldRow;
        TDataColumns                            m_aDataColumns;     // holds the columns as m_pColumns but know the implementation class
        connectivity::ORowSetValue              m_aEmptyValue;      // only for error case

        ::cppu::OWeakObject*                    m_pMySelf;          // set by derived classes
        std::shared_ptr<ORowSetCache>           m_pCache;           // the cache is used by the rowset and his clone (shared)
        std::unique_ptr<ORowSetDataColumns>     m_pColumns;         // represent the select columns
        ::cppu::OBroadcastHelper&               m_rBHelper;         // must be set from the derived classes
        // is used when the formatkey for database types is set
        css::uno::Reference< css::util::XNumberFormatTypes>   m_xNumberFormatTypes;
        std::unique_ptr<OEmptyCollection>       m_pEmptyCollection;

        css::uno::Reference< css::uno::XComponentContext>   m_aContext;
        ::connectivity::SQLError                m_aErrors;

        sal_Int32                               m_nLastColumnIndex; // the last column ask for, used for wasNull()
        sal_Int32                               m_nDeletedPosition; // is set only when a row was deleted
        sal_Int32                               m_nResultSetType;   // fetch property
        sal_Int32                               m_nResultSetConcurrency;
        bool                                    m_bClone;           // I'm clone or not
        bool                                    m_bIgnoreResult ;
        bool                                    m_bBeforeFirst  : 1;
        bool                                    m_bAfterLast    : 1;
        bool                                    m_bIsInsertRow  : 1;

    protected:
        ORowSetBase(
            const css::uno::Reference<css::uno::XComponentContext>& _rContext,
            ::cppu::OBroadcastHelper& _rBHelper,
            ::osl::Mutex* _pMutex
        );

        // fire a notification for all that are listening on column::VALUE property
        void firePropertyChange(const ORowSetRow& _rOldRow);
        // fire a change for one column
        // _nPos starts at zero
        void firePropertyChange(sal_Int32 _nPos,const ::connectivity::ORowSetValue& _rNewValue);

        // fire if rowcount changed
        virtual void fireRowcount();
        // notify row changed
        virtual bool notifyAllListenersCursorBeforeMove(::osl::ResettableMutexGuard& _rGuard);
        // notify cursor moved
        virtual void notifyAllListenersCursorMoved(::osl::ResettableMutexGuard& _rGuard);

        // cancel the insertion, if necessary (means if we're on the insert row)
        virtual void        doCancelModification( ) = 0;
        // return <TRUE/> if and only if we're using the insert row (means: we're updating _or_ inserting)
        virtual bool    isModification( ) = 0;
        // return <TRUE/> if and only if the current row is modified
        // TODO: isn't this the same as isModification?
        virtual bool    isModified( ) = 0;
        // return <TRUE/> if and only if the current row is the insert row
        virtual bool    isNew( ) = 0;
        // return <TRUE/> if the property change notification should be fired
        // upon property change.
        virtual bool    isPropertyChangeNotificationEnabled() const;
        // notify the change of a boolean property
        void fireProperty( sal_Int32 _nProperty, bool _bNew, bool _bOld );

    // OPropertyStateContainer
        virtual void getPropertyDefaultByHandle( sal_Int32 _nHandle, css::uno::Any& _rDefault ) const override;
        virtual void SAL_CALL getFastPropertyValue(css::uno::Any& rValue,sal_Int32 nHandle) const override;

        enum class CursorMoveDirection
        {
            /// denotes a cursor move forward
            Forward,
            /// denotes a cursor  move backwards
            Backward,
            /// denotes no cursor move at all, but move cache to current row (if it is not there already)
            Current,
            /// denotes no cursor move at all, but force the cache to move to current row (and refresh the row)
            CurrentRefresh
        };
        /** positions the cache in preparation of a cursor move

            Normally, the cache is simply moved to our bookmark (m_aBookmark). If however the current
            row is deleted, then the cache is properly positioned for a following cursor movement in the
            given direction.

            @param _ePrepareForDirection
                the direction into which the cursor should be moved after the call. If we're currently not on
                a deleted row, this parameter is ignored, since in this case the cache is simply moved to
                m_aBookmark.</br>
                If, however, we're currently on a deleted row, this is used to properly position the cache
                using <member>m_nDeletedPosition</member>.<br/>
                In this case, MOVE_NONE(_REFRESH) is not supported. This is because the deleted row
                (to which the RowSet currently points to) is not present in the cache. So, you cannot move the
                cache to this row.
        */
        void positionCache( CursorMoveDirection _ePrepareForDirection );

        // returns a value of a column of the current row
        const connectivity::ORowSetValue& getValue(sal_Int32 columnIndex);
        // the cache has to be checked before calling this method
        const connectivity::ORowSetValue& impl_getValue(sal_Int32 columnIndex);
        // sets the current and the bookmark
        void setCurrentRow( bool _bMoved, bool _bDoNotify, const ORowSetRow& _rOldValues, ::osl::ResettableMutexGuard& _rGuard);
        /// @throws css::sdbc::SQLException
        /// @throws css::uno::RuntimeException
        void checkPositioningAllowed();
        // checks  if the cache is null
        void checkCache();
        // sets the bookmark to Any()
        // m_aCurrentRow to end of matrix
        // m_aOldRow to NULL
        void movementFailed();

        ORowSetRow getOldRow(bool _bWasNew);
        /** move the cache the position defined by the member functor
            @param  _aCheckFunctor
                Return <TRUE/> when we already stand on the row we want to.
            @param  _aMovementFunctor
                The method used to move.
            @return
                <TRUE/> if movement was successful.
        */
        bool SAL_CALL move( std::function<bool(ORowSetBase *)> const & _aCheckFunctor,
                            std::function<bool(ORowSetCache *)> const & _aMovementFunctor);

        /** same meaning as isFirst. Only need by mem_fun
            @return
                <TRUE/> if so.
        */
        bool isOnFirst();
        /** same meaning as isLast. Only need by mem_fun
            @return
                <TRUE/> if so.
        */
        bool isOnLast();

        /** returns the current row count

            This function takes into account that we might actually be positioned on a
            deleted row, so that m_pCache->m_nRowCount does not really reflect the actual
            count.

            @precond
                Our mutex is locked.
        */
        sal_Int32   impl_getRowCount() const;

        // the checkCache has to be called before calling this methods
        sal_Int32   impl_getRow();
        bool    impl_rowDeleted();

    public:
        virtual ~ORowSetBase() override;

    // OComponentHelper
        virtual void SAL_CALL disposing();

    // css::beans::XPropertySet
        virtual css::uno::Reference< css::beans::XPropertySetInfo > SAL_CALL getPropertySetInfo(  ) override
        {
            return ::cppu::OPropertySetHelper::createPropertySetInfo(getInfoHelper());
        }

    // comphelper::OPropertyArrayUsageHelper
        virtual ::cppu::IPropertyArrayHelper* createArrayHelper( ) const override;

    // cppu::OPropertySetHelper
        virtual ::cppu::IPropertyArrayHelper& SAL_CALL getInfoHelper() override;

    // css::lang::XTypeProvider
        virtual css::uno::Sequence< css::uno::Type > SAL_CALL getTypes() override;

    // css::uno::XInterface
        virtual css::uno::Any SAL_CALL queryInterface( const css::uno::Type & rType ) override;

    // css::sdbc::XWarningsSupplier
        virtual css::uno::Any SAL_CALL getWarnings(  ) override;
        virtual void SAL_CALL clearWarnings(  ) override;

    // css::sdbc::XResultSetMetaDataSupplier
        virtual css::uno::Reference< css::sdbc::XResultSetMetaData > SAL_CALL getMetaData(  ) override;

    // css::sdbc::XColumnLocate
        virtual sal_Int32 SAL_CALL findColumn( const OUString& columnName ) override;

    // css::sdbcx::XColumnsSupplier
        virtual css::uno::Reference< css::container::XNameAccess > SAL_CALL getColumns(  ) override;

    // css::sdbc::XRow
        virtual sal_Bool SAL_CALL wasNull(  ) override;
        virtual OUString SAL_CALL getString( sal_Int32 columnIndex ) override;
        virtual sal_Bool SAL_CALL getBoolean( sal_Int32 columnIndex ) override;
        virtual sal_Int8 SAL_CALL getByte( sal_Int32 columnIndex ) override;
        virtual sal_Int16 SAL_CALL getShort( sal_Int32 columnIndex ) override;
        virtual sal_Int32 SAL_CALL getInt( sal_Int32 columnIndex ) override;
        virtual sal_Int64 SAL_CALL getLong( sal_Int32 columnIndex ) override;
        virtual float SAL_CALL getFloat( sal_Int32 columnIndex ) override;
        virtual double SAL_CALL getDouble( sal_Int32 columnIndex ) override;
        virtual css::uno::Sequence< sal_Int8 > SAL_CALL getBytes( sal_Int32 columnIndex ) override;
        virtual css::util::Date SAL_CALL getDate( sal_Int32 columnIndex ) override;
        virtual css::util::Time SAL_CALL getTime( sal_Int32 columnIndex ) override;
        virtual css::util::DateTime SAL_CALL getTimestamp( sal_Int32 columnIndex ) override;
        virtual css::uno::Reference< css::io::XInputStream > SAL_CALL getBinaryStream( sal_Int32 columnIndex ) override;
        virtual css::uno::Reference< css::io::XInputStream > SAL_CALL getCharacterStream( sal_Int32 columnIndex ) override;
        virtual css::uno::Any SAL_CALL getObject( sal_Int32 columnIndex, const css::uno::Reference< css::container::XNameAccess >& typeMap ) override;
        virtual css::uno::Reference< css::sdbc::XRef > SAL_CALL getRef( sal_Int32 columnIndex ) override;
        virtual css::uno::Reference< css::sdbc::XBlob > SAL_CALL getBlob( sal_Int32 columnIndex ) override;
        virtual css::uno::Reference< css::sdbc::XClob > SAL_CALL getClob( sal_Int32 columnIndex ) override;
        virtual css::uno::Reference< css::sdbc::XArray > SAL_CALL getArray( sal_Int32 columnIndex ) override;

    // css::sdbcx::XRowLocate
        virtual css::uno::Any SAL_CALL getBookmark(  ) override;
        virtual sal_Bool SAL_CALL moveToBookmark( const css::uno::Any& bookmark ) override;
        virtual sal_Bool SAL_CALL moveRelativeToBookmark( const css::uno::Any& bookmark, sal_Int32 rows ) override;
        virtual sal_Int32 SAL_CALL compareBookmarks( const css::uno::Any& first, const css::uno::Any& second ) override;
        virtual sal_Bool SAL_CALL hasOrderedBookmarks(  ) override;
        virtual sal_Int32 SAL_CALL hashBookmark( const css::uno::Any& bookmark ) override;

    // css::sdbc::XResultSet
        virtual sal_Bool SAL_CALL next(  ) override;
        virtual sal_Bool SAL_CALL isBeforeFirst(  ) override;
        virtual sal_Bool SAL_CALL isAfterLast(  ) override;
        virtual sal_Bool SAL_CALL isFirst(  ) override;
        virtual sal_Bool SAL_CALL isLast(  ) override;
        virtual void SAL_CALL beforeFirst(  ) override;
        virtual void SAL_CALL afterLast(  ) override;
        virtual sal_Bool SAL_CALL first(  ) override;
        virtual sal_Bool SAL_CALL last(  ) override;
        virtual sal_Int32 SAL_CALL getRow(  ) override;
        virtual sal_Bool SAL_CALL absolute( sal_Int32 row ) override;
        virtual sal_Bool SAL_CALL relative( sal_Int32 rows ) override;
        virtual sal_Bool SAL_CALL previous(  ) override;
        virtual void SAL_CALL refreshRow(  ) override;
        virtual sal_Bool SAL_CALL rowUpdated(  ) override;
        virtual sal_Bool SAL_CALL rowInserted(  ) override;
        virtual sal_Bool SAL_CALL rowDeleted(  ) override;
        virtual css::uno::Reference< css::uno::XInterface > SAL_CALL getStatement(  ) override;

    // css::sdbc::XRowSet
        virtual void SAL_CALL execute(  ) override = 0;
        virtual void SAL_CALL addRowSetListener( const css::uno::Reference< css::sdbc::XRowSetListener >& listener ) override = 0;
        virtual void SAL_CALL removeRowSetListener( const css::uno::Reference< css::sdbc::XRowSetListener >& listener ) override = 0;

        // is called when the rowset is going to delete this bookmark _rBookmark
        void onDeleteRow( const css::uno::Any& _rBookmark );
        // is called when the rowset has deleted this bookmark _rBookmark
        void onDeletedRow( const css::uno::Any& _rBookmark, sal_Int32 _nPos );

        // granular access control
        struct GrantNotifierAccess { friend class ORowSetNotifier; private: GrantNotifierAccess () { } };

        // cancel the insertion, if necessary (means if we're on the insert row)
        void        doCancelModification( const GrantNotifierAccess& ) { doCancelModification(); }
        bool    isModification( const GrantNotifierAccess& ) { return isModification(); }
        bool    isModified( const GrantNotifierAccess& ) { return isModified(); }
        bool    isNew( const GrantNotifierAccess& ) { return isNew(); }
        bool    isInsertRow() const { return m_bIsInsertRow; } // isNew() || isModified(); }
        void        fireProperty( sal_Int32 _nProperty, bool _bNew, bool _bOld, const GrantNotifierAccess& )
        {
            fireProperty( _nProperty, _bNew, _bOld );
        }
        void firePropertyChange(sal_Int32 _nPos,const ::connectivity::ORowSetValue& _rNewValue, const GrantNotifierAccess& )
        {
            firePropertyChange(_nPos,_rNewValue);
        }
        using ::comphelper::OPropertyStateContainer::getFastPropertyValue;
    };

    /** eases the handling of the doCancelModification and notifyCancelInsert methods

        <p>The class can only be used on the stack, within a method of ORowSetBase (or derivees)</p>
    */
    class ORowSetNotifier
    {
    private:
        std::vector<sal_Int32>    aChangedColumns;
        ORowSetValueVector::Vector  aRow;
        ORowSetBase*    m_pRowSet;
            // not acquired! This is not necessary because this class here is to be used on the stack within
            // a method of ORowSetBase (or derivees)
        bool        m_bWasNew;
        bool        m_bWasModified;

    public:
        /** constructs the object, and cancels the insertion

            @see ORowSetBase::doCancelModification
        */
        explicit ORowSetNotifier( ORowSetBase* m_pRowSet );

        /** use this one to construct a vector for change value notification
        */
        ORowSetNotifier( ORowSetBase* m_pRowSet, ORowSetValueVector::Vector&& i_aRow );

        // destructs the object. <member>fire</member> has to be called before.
        ~ORowSetNotifier( );

        /** notifies the insertion

            <p>This has <em>not</em> been put into the destructor by intention!<br/>

            The destructor is called during stack unwinding in case of an exception, so if we would do
            listener notification there, this would have the potential of another exception during stack
            unwinding, which would terminate the application.</p>

            @see ORowSetBase::notifyCancelInsert
        */
        void    fire();

        /** notifies value change events and notifies IsModified
            @param  i_aChangedColumns   the index of the changed value columns
            @param  i_aRow              the old values
            @see ORowSetBase::notifyCancelInsert
        */
        void    firePropertyChange();

        /** use this one to store the index of the changed column values
        */
        std::vector<sal_Int32>& getChangedColumns();

    };

} // end of namespace

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
