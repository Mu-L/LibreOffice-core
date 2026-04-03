/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "scriptbrowser.hxx"

namespace singleprovider
{
class ProviderContext;

class ScriptMacro : public ScriptBrowser
{
public:
    ScriptMacro(const std::shared_ptr<ProviderContext>& pProviderContext, const OUString& sName,
                const OUString& sUri);

    // XBrowseNode
    css::uno::Sequence<css::uno::Reference<css::script::browse::XBrowseNode>>
        SAL_CALL getChildNodes() override;
    sal_Bool SAL_CALL hasChildNodes() override;
    sal_Int16 SAL_CALL getType() override;

    // XPropertySet
    css::uno::Any SAL_CALL getPropertyValue(const OUString& sPropertyName) override;

    // XPropertySetInfo
    css::uno::Sequence<css::beans::Property> SAL_CALL getProperties() override;
    css::beans::Property SAL_CALL getPropertyByName(const OUString& sName) override;
    sal_Bool SAL_CALL hasPropertyByName(const OUString& sName) override;

private:
    static css::beans::Property getUriProperty();
};
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
