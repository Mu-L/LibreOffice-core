/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
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

#include <salinst.hxx>
#include <svdata.hxx>
#include <vclpluginapi.h>

#include <list>

class WinSalPrinter;

struct WindowsInstanceData
{
    std::unordered_set<OUString> m_aTempFontPaths;
};

/** Abstract base class for SalInstance implementations on Windows. */
class VCLPLUG_WIN_PUBLIC WindowsInstance : public SalInstance
{
    std::list<WinSalPrinter*> m_aPrinters;

    WindowsInstanceData m_aData;

public:
    WindowsInstance(std::unique_ptr<comphelper::SolarMutex> pMutex, SalData* pSalData);
    virtual ~WindowsInstance();

    virtual SalInfoPrinter* CreateInfoPrinter(SalPrinterQueueInfo& rQueueInfo,
                                              ImplJobSetup& rSetupData) override;
    virtual std::unique_ptr<SalPrinter> CreatePrinter(SalInfoPrinter* pInfoPrinter) override;
    virtual void GetPrinterQueueInfo(ImplPrnQueueList& rList) override;
    virtual void GetPrinterQueueState(SalPrinterQueueInfo* pInfo) override;
    virtual OUString GetDefaultPrinter() override;

    virtual Platform GetPlatform() const override { return Platform::Windows; }

    const std::list<WinSalPrinter*>& GetPrinters() const;
    void InsertPrinter(WinSalPrinter* pPrinter);
    void RemovePrinter(WinSalPrinter* pPrinter);

    WindowsInstanceData& GetData() { return m_aData; }
};

inline WindowsInstance& GetWindowsInstance()
{
    WindowsInstance* pInstance = dynamic_cast<WindowsInstance*>(GetSalInstance());
    assert(pInstance);
    return *pInstance;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
