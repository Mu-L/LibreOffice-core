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

#include <vector>

#include <rtl/ustring.hxx>
#include <optional>

namespace desktop
{

class CommandLineArgs
{
    public:
        struct Supplier
        {
            // Thrown from constructors and next:
            class Exception final
            {
            public:
                Exception();
                Exception(Exception const &);
                Exception & operator =(Exception const &);
            };

            virtual ~Supplier();
            virtual std::optional< OUString > getCwdUrl() = 0;
            virtual bool next(OUString& argument) = 0;
        };

        CommandLineArgs();
        explicit CommandLineArgs( Supplier& supplier );

        CommandLineArgs(const CommandLineArgs&) = delete;
        const CommandLineArgs& operator=(const CommandLineArgs&) = delete;

        const std::optional< OUString >& getCwdUrl() const { return m_cwdUrl; }

        // Access to bool parameters
        bool                IsMinimized() const { return m_minimized;}
        bool                IsInvisible() const
        {
            return m_invisible || m_headless;
        }
        bool                IsNoRestore() const { return m_norestore;}
        bool                IsNoDefault() const { return m_nodefault;}
        bool                IsHeadless() const { return m_headless;}
        bool                IsQuickstart() const { return m_quickstart;}
        bool                IsNoQuickstart() const { return m_noquickstart;}
        bool                IsTerminateAfterInit() const { return m_terminateafterinit;}
        bool                IsNoLogo() const { return m_nologo;}
        bool                IsNoLockcheck() const { return m_nolockcheck;}
        bool                IsHelp() const { return m_help;}
        bool                IsHelpWriter() const { return m_helpwriter;}
        bool                IsHelpCalc() const { return m_helpcalc;}
        bool                IsHelpDraw() const { return m_helpdraw;}
        bool                IsHelpImpress() const { return m_helpimpress;}
        bool                IsHelpBase() const { return m_helpbase;}
        bool                IsHelpMath() const { return m_helpmath;}
        bool                IsHelpBasic() const { return m_helpbasic;}
        bool                IsWriter() const { return m_writer;}
        bool                IsCalc() const { return m_calc;}
        bool                IsDraw() const { return m_draw;}
        bool                IsImpress() const { return m_impress;}
        bool                IsBase() const { return m_base;}
        bool                IsGlobal() const { return m_global;}
        bool                IsMath() const { return m_math;}
        bool                IsWeb() const { return m_web;}
        bool                IsVersion() const { return m_version;}
        bool                HasModuleParam() const;
        bool                WantsToLoadDocument() const { return m_bDocumentArgs;}
        bool                IsTextCat() const { return m_textcat;}
        bool                IsScriptCat() const { return m_scriptcat;}
        bool                IsSafeMode() const { return m_safemode; }

        const OUString&     GetUnknown() const { return m_unknown;}

        // Access to string parameters
        bool                    HasSplashPipe() const { return m_splashpipe;}
        std::vector< OUString > const & GetAccept() const { return m_accept;}
        std::vector< OUString > const & GetUnaccept() const { return m_unaccept;}
        void RemoveFilesFromOpenListEndingWith(const OUString& rExt);
        std::vector< OUString > GetOpenList() const;
        std::vector< OUString > GetViewList() const;
        std::vector< OUString > GetStartList() const;
        std::vector< OUString > GetForceOpenList() const;
        std::vector< OUString > GetForceNewList() const;
        std::vector< OUString > GetPrintList() const;
        std::vector< OUString > GetPrintToList() const;
        const OUString&         GetPrinterName() const { return m_printername;}
        const OUString&         GetLanguage() const { return m_language;}
        std::vector< OUString > const & GetInFilter() const { return m_infilter;}
        std::vector< OUString > GetConversionList() const;
        const OUString&         GetConversionParams() const { return m_conversionparams;}
        OUString                GetConversionOut() const;
        OUString const &        GetImageConversionType() const { return m_convertimages; }
        const OUString&         GetStartListParams() const { return m_startListParams; }
        const OUString&         GetPidfileName() const { return m_pidfile;}

        // Special analyzed states (does not match directly to a command line parameter!)
        bool IsEmpty() const { return m_bEmpty;}

        void setHeadless() { m_headless = true; }

        bool GetAllSucceeded() const { return m_bAllSucceeded; }
        void SetAllSucceeded(bool bSet) { m_bAllSucceeded = bSet; }

    private:
        void                    ParseCommandLine_Impl( Supplier& supplier );
        void                    InitParamValues();

        std::optional< OUString > m_cwdUrl;

        bool m_minimized;
        bool m_invisible;
        bool m_norestore;
        bool m_headless;
        bool m_quickstart;
        bool m_noquickstart;
        bool m_terminateafterinit;
        bool m_nologo;
        bool m_nolockcheck;
        bool m_nodefault;
        bool m_help;
        bool m_writer;
        bool m_calc;
        bool m_draw;
        bool m_impress;
        bool m_global;
        bool m_math;
        bool m_web;
        bool m_base;
        bool m_helpwriter;
        bool m_helpcalc;
        bool m_helpdraw;
        bool m_helpbasic;
        bool m_helpmath;
        bool m_helpimpress;
        bool m_helpbase;
        bool m_version;
        bool m_splashpipe;
        bool m_textcat;
        bool m_scriptcat;
        bool m_safemode;

        OUString m_unknown;

        bool m_bEmpty; // No Args at all
        bool m_bDocumentArgs; // A document creation/open/load arg is used
        std::vector< OUString > m_accept;
        std::vector< OUString > m_unaccept;
        std::vector< OUString > m_openlist; // contains external URIs
        std::vector< OUString > m_viewlist; // contains external URIs
        std::vector< OUString > m_startlist; // contains external URIs
        std::vector< OUString > m_forceopenlist; // contains external URIs
        std::vector< OUString > m_forcenewlist; // contains external URIs
        std::vector< OUString > m_printlist; // contains external URIs
        std::vector< OUString > m_printtolist; // contains external URIs
        OUString m_printername;
        std::vector< OUString > m_conversionlist; // contains external URIs
        OUString m_conversionparams;
        OUString m_conversionout; // contains external URIs
        OUString m_convertimages; // The format in which images should be converted
        OUString m_startListParams;
        std::vector< OUString > m_infilter;
        OUString m_language;
        OUString m_pidfile;

        // If any argument fails, this may be set to false so that the app exits with EXIT_FAILURE.
        // NOTE: only items of interest are setting this to false - can be extended to more cases...
        bool m_bAllSucceeded = true;
};

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
