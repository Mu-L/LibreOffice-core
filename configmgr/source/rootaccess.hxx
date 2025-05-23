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

#include <set>
#include <vector>

#include <com/sun/star/util/XChangesBatch.hpp>
#include <com/sun/star/util/XChangesNotifier.hpp>
#include <rtl/ref.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>

#include "access.hxx"
#include "modifications.hxx"

namespace com::sun::star {
    namespace util { class XChangesListener; }
}

namespace configmgr {

class Components;
class Node;

class RootAccess:
    public Access, public css::util::XChangesNotifier,
    public css::util::XChangesBatch
{
public:
    RootAccess(
        Components & components, OUString pathRepresentation,
        OUString locale, bool update);

    virtual std::vector<OUString> getAbsolutePath() override;

    virtual void initBroadcaster(
        Modifications::Node const & modifications, Broadcaster * broadcaster) override;

    virtual void SAL_CALL acquire() noexcept override;

    virtual void SAL_CALL release() noexcept override;

    OUString const & getAbsolutePathRepresentation();

    const OUString& getLocale() const { return locale_;}

    bool isUpdate() const { return update_;}

    void setAlive(bool b);

    virtual void SAL_CALL addChangesListener(
        css::uno::Reference< css::util::XChangesListener >
            const & aListener) override;

    virtual void SAL_CALL removeChangesListener(
        css::uno::Reference< css::util::XChangesListener >
            const & aListener) override;

    virtual void SAL_CALL commitChanges() override;

    virtual sal_Bool SAL_CALL hasPendingChanges() override;

    virtual css::uno::Sequence< ::css::util::ElementChange > SAL_CALL getPendingChanges() override;

private:
    virtual ~RootAccess() override;

    virtual std::vector<OUString> getRelativePath() override;

    virtual OUString getRelativePathRepresentation() override;

    virtual const rtl::Reference< Node > & getNode() override;

    virtual bool isFinalized() override;

    virtual const OUString & getNameInternal() override;

    virtual rtl::Reference< RootAccess > getRootAccess() override;

    virtual rtl::Reference< Access > getParentAccess() override;

    virtual void addTypes(std::vector< css::uno::Type > * types)
        const override;

    virtual void addSupportedServiceNames(
        std::vector<OUString> * services) override;

    virtual void initDisposeBroadcaster(Broadcaster * broadcaster) override;

    virtual void clearListeners() noexcept override;

    virtual css::uno::Any SAL_CALL queryInterface(
        css::uno::Type const & aType) override;

    virtual OUString SAL_CALL getImplementationName() override;

    typedef
        std::multiset<
            css::uno::Reference<
                css::util::XChangesListener > >
        ChangesListeners;

    OUString pathRepresentation_;
    OUString locale_;
    std::vector<OUString> path_;
    rtl::Reference< Node > node_;
    OUString name_;
    ChangesListeners changesListeners_;

    std::shared_ptr<osl::Mutex> lock_;

    bool update_:1;
    bool finalized_:1;
    bool alive_:1;
};

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
