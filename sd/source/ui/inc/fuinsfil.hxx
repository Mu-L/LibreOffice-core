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

#include "fupoor.hxx"
#include <vector>
#include <rtl/ustring.hxx>

class SfxMedium;

namespace sd {

class FuInsertFile final
    : public FuPoor
{
public:

    static rtl::Reference<FuPoor> Create( ViewShell& rViewSh, ::sd::Window* pWin, ::sd::View* pView, SdDrawDocument& rDoc, SfxRequest& rReq );
    virtual void DoExecute( SfxRequest& rReq ) override;

    static void     GetSupportedFilterVector( ::std::vector< OUString >& rFilterVector );

private:
    FuInsertFile (
        ViewShell& rViewSh,
        ::sd::Window* pWin,
        ::sd::View* pView,
        SdDrawDocument& rDoc,
        SfxRequest& rReq);

    OUString        aLayoutName;    ///< layout name of the currently inserted page
    OUString        aFilterName;    ///< chosen file filter
    OUString        aFile;          ///< chosen file name

    void            InsTextOrRTFinOlMode(SfxMedium* pMedium);
    bool            InsSDDinOlMode(SfxMedium* pMedium);
    void            InsTextOrRTFinDrMode(SfxMedium* pMedium);
    bool            InsSDDinDrMode(SfxMedium* pMedium);
};

} // end of namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
