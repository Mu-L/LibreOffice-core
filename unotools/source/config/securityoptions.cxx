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

#include <unotools/securityoptions.hxx>
#include <unotools/configmgr.hxx>
#include <unotools/configitem.hxx>
#include <unotools/ucbhelper.hxx>
#include <tools/debug.hxx>
#include <com/sun/star/uno/Any.hxx>
#include <com/sun/star/uno/Sequence.hxx>

#include <com/sun/star/beans/PropertyValue.hpp>
#include <comphelper/sequence.hxx>
#include <tools/urlobj.hxx>

#include <unotools/pathoptions.hxx>

#include "itemholder1.hxx"

//  namespaces

using namespace ::utl;
using namespace ::osl;
using namespace ::com::sun::star::uno;

#define ROOTNODE_SECURITY               "Office.Common/Security/Scripting"
#define DEFAULT_SECUREURL               Sequence< OUString >()
#define DEFAULT_TRUSTEDAUTHORS          std::vector< SvtSecurityOptions::Certificate >()

#define PROPERTYNAME_SECUREURL                  u"SecureURL"
#define PROPERTYNAME_DOCWARN_SAVEORSEND         u"WarnSaveOrSendDoc"
#define PROPERTYNAME_DOCWARN_SIGNING            u"WarnSignDoc"
#define PROPERTYNAME_DOCWARN_PRINT              u"WarnPrintDoc"
#define PROPERTYNAME_DOCWARN_CREATEPDF          u"WarnCreatePDF"
#define PROPERTYNAME_DOCWARN_REMOVEPERSONALINFO u"RemovePersonalInfoOnSaving"
#define PROPERTYNAME_DOCWARN_RECOMMENDPASSWORD  u"RecommendPasswordProtection"
#define PROPERTYNAME_CTRLCLICK_HYPERLINK        u"HyperlinksWithCtrlClick"
#define PROPERTYNAME_BLOCKUNTRUSTEDREFERERLINKS u"BlockUntrustedRefererLinks"
#define PROPERTYNAME_MACRO_SECLEVEL             u"MacroSecurityLevel"
#define PROPERTYNAME_MACRO_TRUSTEDAUTHORS       u"TrustedAuthors"
#define PROPERTYNAME_MACRO_DISABLE              u"DisableMacrosExecution"
#define PROPERTYNAME_TRUSTEDAUTHOR_SUBJECTNAME  u"SubjectName"
#define PROPERTYNAME_TRUSTEDAUTHOR_SERIALNUMBER u"SerialNumber"
#define PROPERTYNAME_TRUSTEDAUTHOR_RAWDATA      u"RawData"

#define PROPERTYHANDLE_SECUREURL                    0

#define PROPERTYHANDLE_DOCWARN_SAVEORSEND           1
#define PROPERTYHANDLE_DOCWARN_SIGNING              2
#define PROPERTYHANDLE_DOCWARN_PRINT                3
#define PROPERTYHANDLE_DOCWARN_CREATEPDF            4
#define PROPERTYHANDLE_DOCWARN_REMOVEPERSONALINFO   5
#define PROPERTYHANDLE_DOCWARN_RECOMMENDPASSWORD    6
#define PROPERTYHANDLE_CTRLCLICK_HYPERLINK          7
#define PROPERTYHANDLE_BLOCKUNTRUSTEDREFERERLINKS   8
#define PROPERTYHANDLE_MACRO_SECLEVEL               9
#define PROPERTYHANDLE_MACRO_TRUSTEDAUTHORS         10
#define PROPERTYHANDLE_MACRO_DISABLE                11

#define PROPERTYHANDLE_INVALID                      -1

#define CFG_READONLY_DEFAULT                        false

//  private declarations!

class SvtSecurityOptions_Impl : public ConfigItem
{

    private:
        virtual void ImplCommit() override;

    //  public methods

    public:

        //  constructor / destructor

         SvtSecurityOptions_Impl();
        virtual ~SvtSecurityOptions_Impl() override;

        //  override methods of baseclass

        /*-****************************************************************************************************
            @short      called for notify of configmanager
            @descr      This method is called from the ConfigManager before application ends or from the
                        PropertyChangeListener if the sub tree broadcasts changes. You must update your
                        internal values.

            @seealso    baseclass ConfigItem

            @param      "seqPropertyNames" is the list of properties which should be updated.
        *//*-*****************************************************************************************************/

        virtual void Notify( const Sequence< OUString >& seqPropertyNames ) override;

        //  public interface

        bool                IsReadOnly      ( SvtSecurityOptions::EOption eOption                   ) const;

        const Sequence< OUString >& GetSecureURLs(                                                       ) const { return m_seqSecureURLs;}
        void                    SetSecureURLs    (   const   Sequence< OUString >&   seqURLList          );
        inline sal_Int32        GetMacroSecurityLevel   (                                               ) const;
        void                    SetMacroSecurityLevel   ( sal_Int32 _nLevel                             );

        inline bool         IsMacroDisabled         (                                               ) const;

        const std::vector< SvtSecurityOptions::Certificate >& GetTrustedAuthors() const { return m_seqTrustedAuthors;}
        void                SetTrustedAuthors       ( const std::vector< SvtSecurityOptions::Certificate >& rAuthors );

        bool                IsOptionSet     ( SvtSecurityOptions::EOption eOption                   ) const;
        void                SetOption       ( SvtSecurityOptions::EOption eOption, bool bValue  );
        bool                IsOptionEnabled ( SvtSecurityOptions::EOption eOption                   ) const;


        void                    SetProperty( sal_Int32 nHandle, const Any& rValue, bool bReadOnly );
        void                    LoadAuthors();
        static sal_Int32        GetHandle( std::u16string_view rPropertyName );
        bool                    GetOption( SvtSecurityOptions::EOption eOption, bool*& rpValue, bool*& rpRO );

        /*-****************************************************************************************************
            @short      return list of key names of our configuration management which represent our module tree
            @descr      This method returns a static const list of key names. We need it to get needed values from our
                        configuration management.
            @return     A list of needed configuration keys is returned.
        *//*-*****************************************************************************************************/
        static Sequence< OUString > GetPropertyNames();

        Sequence< OUString >                        m_seqSecureURLs;
        bool                                    m_bSaveOrSend;
        bool                                    m_bSigning;
        bool                                    m_bPrint;
        bool                                    m_bCreatePDF;
        bool                                    m_bRemoveInfo;
        bool                                    m_bRecommendPwd;
        bool                                    m_bCtrlClickHyperlink;
        bool                                    m_bBlockUntrustedRefererLinks;
        sal_Int32                                   m_nSecLevel;
        std::vector< SvtSecurityOptions::Certificate > m_seqTrustedAuthors;
        bool                                    m_bDisableMacros;

        bool                                    m_bROSecureURLs;
        bool                                    m_bROSaveOrSend;
        bool                                    m_bROSigning;
        bool                                    m_bROPrint;
        bool                                    m_bROCreatePDF;
        bool                                    m_bRORemoveInfo;
        bool                                    m_bRORecommendPwd;
        bool                                    m_bROCtrlClickHyperlink;
        bool                                    m_bROBlockUntrustedRefererLinks;
        bool                                    m_bROSecLevel;
        bool                                    m_bROTrustedAuthors;
        bool                                    m_bRODisableMacros;
};

//  constructor

SvtSecurityOptions_Impl::SvtSecurityOptions_Impl()
    :ConfigItem             ( ROOTNODE_SECURITY         )
    ,m_seqSecureURLs        ( DEFAULT_SECUREURL         )
    ,m_bSaveOrSend          ( true                  )
    ,m_bSigning             ( true                  )
    ,m_bPrint               ( true                  )
    ,m_bCreatePDF           ( true                  )
    ,m_bRemoveInfo          ( true                  )
    ,m_bRecommendPwd(false)
    ,m_bCtrlClickHyperlink(false)
    ,m_bBlockUntrustedRefererLinks(false)
    ,m_nSecLevel            ( 1                     )
    ,m_seqTrustedAuthors    ( DEFAULT_TRUSTEDAUTHORS    )
    ,m_bDisableMacros       ( false                 )
    ,m_bROSecureURLs        ( CFG_READONLY_DEFAULT      )
    ,m_bROSaveOrSend        ( CFG_READONLY_DEFAULT      )
    ,m_bROSigning           ( CFG_READONLY_DEFAULT      )
    ,m_bROPrint             ( CFG_READONLY_DEFAULT      )
    ,m_bROCreatePDF         ( CFG_READONLY_DEFAULT      )
    ,m_bRORemoveInfo        ( CFG_READONLY_DEFAULT      )
    ,m_bRORecommendPwd(CFG_READONLY_DEFAULT)
    ,m_bROCtrlClickHyperlink(CFG_READONLY_DEFAULT)
    ,m_bROBlockUntrustedRefererLinks(CFG_READONLY_DEFAULT)
    ,m_bROSecLevel          ( CFG_READONLY_DEFAULT      )
    ,m_bROTrustedAuthors    ( CFG_READONLY_DEFAULT      )
    ,m_bRODisableMacros     ( true                  ) // currently is not intended to be changed
{
    Sequence< OUString >    seqNames    = GetPropertyNames  (           );
    Sequence< Any >         seqValues   = GetProperties     ( seqNames  );
    Sequence< sal_Bool >    seqRO       = GetReadOnlyStates ( seqNames  );

    // Safe impossible cases.
    // We need values from ALL configuration keys.
    // Follow assignment use order of values in relation to our list of key names!
    assert( seqNames.getLength() == seqValues.getLength() && "I miss some values of configuration keys!" );

    // Copy values from list in right order to our internal member.
    sal_Int32               nPropertyCount = seqValues.getLength();
    for( sal_Int32 nProperty = 0; nProperty < nPropertyCount; ++nProperty )
        SetProperty( nProperty, seqValues[ nProperty ], seqRO[ nProperty ] );

    LoadAuthors();

    // Enable notification mechanism of our baseclass.
    // We need it to get information about changes outside these class on our used configuration keys!*/

    EnableNotification( seqNames );
}

//  destructor

SvtSecurityOptions_Impl::~SvtSecurityOptions_Impl()
{
    assert(!IsModified()); // should have been committed
}

void SvtSecurityOptions_Impl::SetProperty( sal_Int32 nProperty, const Any& rValue, bool bRO )
{
    switch( nProperty )
    {
        case PROPERTYHANDLE_SECUREURL:
        {
            m_seqSecureURLs.realloc( 0 );
            rValue >>= m_seqSecureURLs;
            if (!utl::ConfigManager::IsFuzzing())
            {
                SvtPathOptions  aOpt;
                std::transform(m_seqSecureURLs.begin(), m_seqSecureURLs.end(), m_seqSecureURLs.begin(),
                    [&aOpt](const OUString& rUrl) -> OUString { return aOpt.SubstituteVariable( rUrl ); });
            }
            m_bROSecureURLs = bRO;
        }
        break;

        case PROPERTYHANDLE_DOCWARN_SAVEORSEND:
        {
            rValue >>= m_bSaveOrSend;
            m_bROSaveOrSend = bRO;
        }
        break;

        case PROPERTYHANDLE_DOCWARN_SIGNING:
        {
            rValue >>= m_bSigning;
            m_bROSigning = bRO;
        }
        break;

        case PROPERTYHANDLE_DOCWARN_PRINT:
        {
            rValue >>= m_bPrint;
            m_bROPrint = bRO;
        }
        break;

        case PROPERTYHANDLE_DOCWARN_CREATEPDF:
        {
            rValue >>= m_bCreatePDF;
            m_bROCreatePDF = bRO;
        }
        break;

        case PROPERTYHANDLE_DOCWARN_REMOVEPERSONALINFO:
        {
            rValue >>= m_bRemoveInfo;
            m_bRORemoveInfo = bRO;
        }
        break;

        case PROPERTYHANDLE_DOCWARN_RECOMMENDPASSWORD:
        {
            rValue >>= m_bRecommendPwd;
            m_bRORecommendPwd = bRO;
        }
        break;

        case PROPERTYHANDLE_CTRLCLICK_HYPERLINK:
        {
            rValue >>= m_bCtrlClickHyperlink;
            m_bROCtrlClickHyperlink = bRO;
        }
        break;

        case PROPERTYHANDLE_BLOCKUNTRUSTEDREFERERLINKS:
        {
            rValue >>= m_bBlockUntrustedRefererLinks;
            m_bROBlockUntrustedRefererLinks = bRO;
        }
        break;

        case PROPERTYHANDLE_MACRO_SECLEVEL:
        {
            rValue >>= m_nSecLevel;
            m_bROSecLevel = bRO;
        }
        break;

        case PROPERTYHANDLE_MACRO_TRUSTEDAUTHORS:
        {
            // don't care about value here...
            m_bROTrustedAuthors = bRO;
        }
        break;

        case PROPERTYHANDLE_MACRO_DISABLE:
        {
            rValue >>= m_bDisableMacros;
            m_bRODisableMacros = bRO;
        }
        break;

#if OSL_DEBUG_LEVEL > 0
        default:
            assert(false && "Unknown property!");
#endif
        }
}

void SvtSecurityOptions_Impl::LoadAuthors()
{
    m_seqTrustedAuthors.clear();       // first clear
    const Sequence< OUString > lAuthors = GetNodeNames( PROPERTYNAME_MACRO_TRUSTEDAUTHORS );
    sal_Int32               c1 = lAuthors.getLength();
    if( !c1 )
        return;

    sal_Int32               c2 = c1 * 3;                // 3 Properties inside Struct TrustedAuthor
    Sequence< OUString >    lAllAuthors( c2 );

    sal_Int32               i2 = 0;
    OUString                aSep( "/" );
    for( const auto& rAuthor : lAuthors )
    {
        lAllAuthors[ i2 ] = PROPERTYNAME_MACRO_TRUSTEDAUTHORS + aSep + rAuthor + aSep + PROPERTYNAME_TRUSTEDAUTHOR_SUBJECTNAME;
        ++i2;
        lAllAuthors[ i2 ] = PROPERTYNAME_MACRO_TRUSTEDAUTHORS + aSep + rAuthor + aSep + PROPERTYNAME_TRUSTEDAUTHOR_SERIALNUMBER;
        ++i2;
        lAllAuthors[ i2 ] = PROPERTYNAME_MACRO_TRUSTEDAUTHORS + aSep + rAuthor + aSep + PROPERTYNAME_TRUSTEDAUTHOR_RAWDATA;
        ++i2;
    }

    Sequence< Any >         lValues = GetProperties( lAllAuthors );
    if( lValues.getLength() != c2 )
        return;

    std::vector< SvtSecurityOptions::Certificate > v;
    SvtSecurityOptions::Certificate aCert;
    i2 = 0;
    for( sal_Int32 i1 = 0; i1 < c1; ++i1 )
    {
        lValues[ i2 ] >>= aCert.SubjectName;
        ++i2;
        lValues[ i2 ] >>= aCert.SerialNumber;
        ++i2;
        lValues[ i2 ] >>= aCert.RawData;
        ++i2;
        // Filter out TrustedAuthor entries with empty RawData, which
        // would cause an unexpected std::bad_alloc in
        // SecurityEnvironment_NssImpl::createCertificateFromAscii and
        // have been observed in the wild (fdo#55019):
        if( !aCert.RawData.isEmpty() )
        {
            v.push_back( aCert );
        }
    }
    m_seqTrustedAuthors = v;
}

sal_Int32 SvtSecurityOptions_Impl::GetHandle( std::u16string_view rName )
{
    sal_Int32   nHandle;

    if( rName == PROPERTYNAME_SECUREURL )
        nHandle = PROPERTYHANDLE_SECUREURL;
    else if( rName == PROPERTYNAME_DOCWARN_SAVEORSEND )
        nHandle = PROPERTYHANDLE_DOCWARN_SAVEORSEND;
    else if( rName == PROPERTYNAME_DOCWARN_SIGNING )
        nHandle = PROPERTYHANDLE_DOCWARN_SIGNING;
    else if( rName == PROPERTYNAME_DOCWARN_PRINT )
        nHandle = PROPERTYHANDLE_DOCWARN_PRINT;
    else if( rName == PROPERTYNAME_DOCWARN_CREATEPDF )
        nHandle = PROPERTYHANDLE_DOCWARN_CREATEPDF;
    else if( rName == PROPERTYNAME_DOCWARN_REMOVEPERSONALINFO )
        nHandle = PROPERTYHANDLE_DOCWARN_REMOVEPERSONALINFO;
    else if( rName == PROPERTYNAME_DOCWARN_RECOMMENDPASSWORD )
        nHandle = PROPERTYHANDLE_DOCWARN_RECOMMENDPASSWORD;
    else if( rName == PROPERTYNAME_CTRLCLICK_HYPERLINK )
        nHandle = PROPERTYHANDLE_CTRLCLICK_HYPERLINK;
    else if( rName == PROPERTYNAME_BLOCKUNTRUSTEDREFERERLINKS )
        nHandle = PROPERTYHANDLE_BLOCKUNTRUSTEDREFERERLINKS;
    else if( rName == PROPERTYNAME_MACRO_SECLEVEL )
        nHandle = PROPERTYHANDLE_MACRO_SECLEVEL;
    else if( rName == PROPERTYNAME_MACRO_TRUSTEDAUTHORS )
        nHandle = PROPERTYHANDLE_MACRO_TRUSTEDAUTHORS;
    else if( rName == PROPERTYNAME_MACRO_DISABLE )
        nHandle = PROPERTYHANDLE_MACRO_DISABLE;

    else
        nHandle = PROPERTYHANDLE_INVALID;

    return nHandle;
}

bool SvtSecurityOptions_Impl::GetOption( SvtSecurityOptions::EOption eOption, bool*& rpValue, bool*& rpRO )
{
    switch( eOption )
    {
        case SvtSecurityOptions::EOption::DocWarnSaveOrSend:
            rpValue = &m_bSaveOrSend;
            rpRO = &m_bROSaveOrSend;
            break;
        case SvtSecurityOptions::EOption::DocWarnSigning:
            rpValue = &m_bSigning;
            rpRO = &m_bROSigning;
            break;
        case SvtSecurityOptions::EOption::DocWarnPrint:
            rpValue = &m_bPrint;
            rpRO = &m_bROPrint;
            break;
        case SvtSecurityOptions::EOption::DocWarnCreatePdf:
            rpValue = &m_bCreatePDF;
            rpRO = &m_bROCreatePDF;
            break;
        case SvtSecurityOptions::EOption::DocWarnRemovePersonalInfo:
            rpValue = &m_bRemoveInfo;
            rpRO = &m_bRORemoveInfo;
            break;
        case SvtSecurityOptions::EOption::DocWarnRecommendPassword:
            rpValue = &m_bRecommendPwd;
            rpRO = &m_bRORecommendPwd;
            break;
        case SvtSecurityOptions::EOption::CtrlClickHyperlink:
            rpValue = &m_bCtrlClickHyperlink;
            rpRO = &m_bROCtrlClickHyperlink;
            break;
        case SvtSecurityOptions::EOption::BlockUntrustedRefererLinks:
            rpValue = &m_bBlockUntrustedRefererLinks;
            rpRO = &m_bROBlockUntrustedRefererLinks;
            break;
        default:
            rpValue = nullptr;
            rpRO = nullptr;
            break;
    }

    return rpValue != nullptr;
}

void SvtSecurityOptions_Impl::Notify( const Sequence< OUString >& seqPropertyNames )
{
    // Use given list of updated properties to get his values from configuration directly!
    Sequence< Any >         seqValues = GetProperties( seqPropertyNames );
    Sequence< sal_Bool >    seqRO = GetReadOnlyStates( seqPropertyNames );
    // Safe impossible cases.
    // We need values from ALL notified configuration keys.
    DBG_ASSERT( !(seqPropertyNames.getLength()!=seqValues.getLength()), "SvtSecurityOptions_Impl::Notify()\nI miss some values of configuration keys!\n" );
    // Step over list of property names and get right value from corresponding value list to set it on internal members!
    sal_Int32               nCount = seqPropertyNames.getLength();
    for( sal_Int32 nProperty = 0; nProperty < nCount; ++nProperty )
        SetProperty( GetHandle( seqPropertyNames[ nProperty ] ), seqValues[ nProperty ], seqRO[ nProperty ] );

    // read set of trusted authors separately
    LoadAuthors();
}

void SvtSecurityOptions_Impl::ImplCommit()
{
    // Get names of supported properties, create a list for values and copy current values to it.
    Sequence< OUString >    lOrgNames = GetPropertyNames();
    sal_Int32               nOrgCount = lOrgNames.getLength();

    Sequence< OUString >    lNames(nOrgCount);
    Sequence< Any >         lValues(nOrgCount);
    sal_Int32               nRealCount = 0;
    bool                    bDone;

    ClearNodeSet( PROPERTYNAME_MACRO_TRUSTEDAUTHORS );

    for( sal_Int32 nProperty = 0; nProperty < nOrgCount; ++nProperty )
    {
        switch( nProperty )
        {
            case PROPERTYHANDLE_SECUREURL:
            {
                bDone = !m_bROSecureURLs;
                if( bDone )
                {
                    Sequence< OUString >    lURLs( m_seqSecureURLs );
                    SvtPathOptions          aOpt;
                    std::transform(lURLs.begin(), lURLs.end(), lURLs.begin(),
                        [&aOpt](const OUString& rUrl) -> OUString { return aOpt.UseVariable( rUrl ); });
                    lValues[ nRealCount ] <<= lURLs;
                }
            }
            break;

            case PROPERTYHANDLE_DOCWARN_SAVEORSEND:
            {
                bDone = !m_bROSaveOrSend;
                if( bDone )
                    lValues[ nRealCount ] <<= m_bSaveOrSend;
            }
            break;

            case PROPERTYHANDLE_DOCWARN_SIGNING:
            {
                bDone = !m_bROSigning;
                if( bDone )
                    lValues[ nRealCount ] <<= m_bSigning;
            }
            break;

            case PROPERTYHANDLE_DOCWARN_PRINT:
            {
                bDone = !m_bROPrint;
                if( bDone )
                    lValues[ nRealCount ] <<= m_bPrint;
            }
            break;

            case PROPERTYHANDLE_DOCWARN_CREATEPDF:
            {
                bDone = !m_bROCreatePDF;
                if( bDone )
                    lValues[ nRealCount ] <<= m_bCreatePDF;
            }
            break;

            case PROPERTYHANDLE_DOCWARN_REMOVEPERSONALINFO:
            {
                bDone = !m_bRORemoveInfo;
                if( bDone )
                    lValues[ nRealCount ] <<= m_bRemoveInfo;
            }
            break;

            case PROPERTYHANDLE_DOCWARN_RECOMMENDPASSWORD:
            {
                bDone = !m_bRORecommendPwd;
                if( bDone )
                    lValues[ nRealCount ] <<= m_bRecommendPwd;
            }
            break;

            case PROPERTYHANDLE_CTRLCLICK_HYPERLINK:
            {
                bDone = !m_bROCtrlClickHyperlink;
                if( bDone )
                    lValues[ nRealCount ] <<= m_bCtrlClickHyperlink;
            }
            break;

            case PROPERTYHANDLE_BLOCKUNTRUSTEDREFERERLINKS:
            {
                bDone = !m_bROBlockUntrustedRefererLinks;
                if( bDone )
                    lValues[ nRealCount ] <<= m_bBlockUntrustedRefererLinks;
            }
            break;

            case PROPERTYHANDLE_MACRO_SECLEVEL:
            {
                bDone = !m_bROSecLevel;
                if( bDone )
                    lValues[ nRealCount ] <<= m_nSecLevel;
            }
            break;

            case PROPERTYHANDLE_MACRO_TRUSTEDAUTHORS:
            {
                bDone = !m_bROTrustedAuthors;
                if( bDone )
                {
                    sal_Int32   nCnt = m_seqTrustedAuthors.size();
                    if( nCnt )
                    {
                        for( sal_Int32 i = 0; i < nCnt; ++i )
                        {
                            OUString aPrefix(
                                PROPERTYNAME_MACRO_TRUSTEDAUTHORS "/a"
                                + OUString::number(i) + "/");
                            Sequence< css::beans::PropertyValue >    lPropertyValues( 3 );
                            lPropertyValues[ 0 ].Name = aPrefix + PROPERTYNAME_TRUSTEDAUTHOR_SUBJECTNAME;
                            lPropertyValues[ 0 ].Value <<= m_seqTrustedAuthors[ i ].SubjectName;
                            lPropertyValues[ 1 ].Name = aPrefix + PROPERTYNAME_TRUSTEDAUTHOR_SERIALNUMBER;
                            lPropertyValues[ 1 ].Value <<= m_seqTrustedAuthors[ i ].SerialNumber;
                            lPropertyValues[ 2 ].Name = aPrefix + PROPERTYNAME_TRUSTEDAUTHOR_RAWDATA;
                            lPropertyValues[ 2 ].Value <<= m_seqTrustedAuthors[ i ].RawData;

                            SetSetProperties( PROPERTYNAME_MACRO_TRUSTEDAUTHORS, lPropertyValues );
                        }

                        bDone = false;      // because we save in loop above!
                    }
                    else
                        bDone = false;
                }
            }
            break;

            case PROPERTYHANDLE_MACRO_DISABLE:
            {
                bDone = !m_bRODisableMacros;
                if( bDone )
                    lValues[ nRealCount ] <<= m_bDisableMacros;
            }
            break;

            default:
                bDone = false;
        }

        if( bDone )
        {
            lNames[ nRealCount ] = lOrgNames[ nProperty ];
            ++nRealCount;
        }
    }
    // Set properties in configuration.
    lNames.realloc(nRealCount);
    lValues.realloc(nRealCount);
    PutProperties( lNames, lValues );
}

bool SvtSecurityOptions_Impl::IsReadOnly( SvtSecurityOptions::EOption eOption ) const
{
    bool    bReadonly;
    switch(eOption)
    {
        case SvtSecurityOptions::EOption::SecureUrls :
            bReadonly = m_bROSecureURLs;
            break;
        case SvtSecurityOptions::EOption::DocWarnSaveOrSend:
            bReadonly = m_bROSaveOrSend;
            break;
        case SvtSecurityOptions::EOption::DocWarnSigning:
            bReadonly = m_bROSigning;
            break;
        case SvtSecurityOptions::EOption::DocWarnPrint:
            bReadonly = m_bROPrint;
            break;
        case SvtSecurityOptions::EOption::DocWarnCreatePdf:
            bReadonly = m_bROCreatePDF;
            break;
        case SvtSecurityOptions::EOption::DocWarnRemovePersonalInfo:
            bReadonly = m_bRORemoveInfo;
            break;
        case SvtSecurityOptions::EOption::DocWarnRecommendPassword:
            bReadonly = m_bRORecommendPwd;
            break;
        case SvtSecurityOptions::EOption::MacroSecLevel:
            bReadonly = m_bROSecLevel;
            break;
        case SvtSecurityOptions::EOption::MacroTrustedAuthors:
            bReadonly = m_bROTrustedAuthors;
            break;
        case SvtSecurityOptions::EOption::CtrlClickHyperlink:
            bReadonly = m_bROCtrlClickHyperlink;
            break;
        case SvtSecurityOptions::EOption::BlockUntrustedRefererLinks:
            bReadonly = m_bROBlockUntrustedRefererLinks;
            break;

        default:
            bReadonly = true;
    }

    return bReadonly;
}


void SvtSecurityOptions_Impl::SetSecureURLs( const Sequence< OUString >& seqURLList )
{
    DBG_ASSERT(!m_bROSecureURLs, "SvtSecurityOptions_Impl::SetSecureURLs()\nYou tried to write on a readonly value!\n");
    if (!m_bROSecureURLs && m_seqSecureURLs!=seqURLList)
    {
        m_seqSecureURLs = seqURLList;
        SetModified();
    }
}

inline sal_Int32 SvtSecurityOptions_Impl::GetMacroSecurityLevel() const
{
    return m_nSecLevel;
}

inline bool SvtSecurityOptions_Impl::IsMacroDisabled() const
{
    return m_bDisableMacros;
}

void SvtSecurityOptions_Impl::SetMacroSecurityLevel( sal_Int32 _nLevel )
{
    if( !m_bROSecLevel )
    {
        if( _nLevel > 3 || _nLevel < 0 )
            _nLevel = 3;

        if( m_nSecLevel != _nLevel )
        {
            m_nSecLevel = _nLevel;
            SetModified();
        }
    }
}


void SvtSecurityOptions_Impl::SetTrustedAuthors( const std::vector< SvtSecurityOptions::Certificate >& rAuthors )
{
    DBG_ASSERT(!m_bROTrustedAuthors, "SvtSecurityOptions_Impl::SetTrustedAuthors()\nYou tried to write on a readonly value!\n");
    if( !m_bROTrustedAuthors && rAuthors != m_seqTrustedAuthors )
    {
        m_seqTrustedAuthors = rAuthors;
        SetModified();
    }
}

bool SvtSecurityOptions_Impl::IsOptionSet( SvtSecurityOptions::EOption eOption ) const
{
    bool*   pValue;
    bool*   pRO;
    bool    bRet = false;

    if( const_cast< SvtSecurityOptions_Impl* >( this )->GetOption( eOption, pValue, pRO ) )
        bRet = *pValue;

    return bRet;
}

void SvtSecurityOptions_Impl::SetOption( SvtSecurityOptions::EOption eOption, bool bValue )
{
    bool*   pValue;
    bool*   pRO;

    if( GetOption( eOption, pValue, pRO ) && !*pRO && *pValue != bValue)
    {
        *pValue = bValue;
        SetModified();
    }
}

bool SvtSecurityOptions_Impl::IsOptionEnabled( SvtSecurityOptions::EOption eOption ) const
{
    bool*   pValue;
    bool*   pRO;
    bool    bRet = false;

    if( const_cast< SvtSecurityOptions_Impl* >( this )->GetOption( eOption, pValue, pRO ) )
        bRet = !*pRO;

    return bRet;
}

Sequence< OUString > SvtSecurityOptions_Impl::GetPropertyNames()
{
    return Sequence< OUString >
    {
        PROPERTYNAME_SECUREURL,
        PROPERTYNAME_DOCWARN_SAVEORSEND,
        PROPERTYNAME_DOCWARN_SIGNING,
        PROPERTYNAME_DOCWARN_PRINT,
        PROPERTYNAME_DOCWARN_CREATEPDF,
        PROPERTYNAME_DOCWARN_REMOVEPERSONALINFO,
        PROPERTYNAME_DOCWARN_RECOMMENDPASSWORD,
        PROPERTYNAME_CTRLCLICK_HYPERLINK,
        PROPERTYNAME_BLOCKUNTRUSTEDREFERERLINKS,
        PROPERTYNAME_MACRO_SECLEVEL,
        PROPERTYNAME_MACRO_TRUSTEDAUTHORS,
        PROPERTYNAME_MACRO_DISABLE
    };
}

namespace {

std::weak_ptr<SvtSecurityOptions_Impl> g_pSecurityOptions;

}

SvtSecurityOptions::SvtSecurityOptions()
{
    // Global access, must be guarded (multithreading!).
    MutexGuard aGuard( GetInitMutex() );

    m_pImpl = g_pSecurityOptions.lock();
    if( !m_pImpl )
    {
        m_pImpl = std::make_shared<SvtSecurityOptions_Impl>();
        g_pSecurityOptions = m_pImpl;

        ItemHolder1::holdConfigItem(EItem::SecurityOptions);
    }
}

SvtSecurityOptions::~SvtSecurityOptions()
{
    // Global access, must be guarded (multithreading!)
    MutexGuard aGuard( GetInitMutex() );

    m_pImpl.reset();
}

bool SvtSecurityOptions::IsReadOnly( EOption eOption ) const
{
    MutexGuard aGuard( GetInitMutex() );
    return m_pImpl->IsReadOnly(eOption);
}

Sequence< OUString > SvtSecurityOptions::GetSecureURLs() const
{
    MutexGuard aGuard( GetInitMutex() );
    return m_pImpl->GetSecureURLs();
}

void SvtSecurityOptions::SetSecureURLs( const Sequence< OUString >& seqURLList )
{
    MutexGuard aGuard( GetInitMutex() );
    m_pImpl->SetSecureURLs( seqURLList );
}

bool SvtSecurityOptions::isSecureMacroUri(
    OUString const & uri, OUString const & referer) const
{
    switch (INetURLObject(uri).GetProtocol()) {
    case INetProtocol::Macro:
        if (uri.startsWithIgnoreAsciiCase("macro:///")) {
            // Denotes an App-BASIC macro (see SfxMacroLoader::loadMacro), which
            // is considered safe:
            return true;
        }
        [[fallthrough]];
    case INetProtocol::Slot:
        return referer.equalsIgnoreAsciiCase("private:user")
            || isTrustedLocationUri(referer);
    default:
        return true;
    }
}

bool SvtSecurityOptions::isUntrustedReferer(OUString const & referer) const {
    MutexGuard g(GetInitMutex());
    return m_pImpl->IsOptionSet(EOption::BlockUntrustedRefererLinks)
        && !(referer.isEmpty() || referer.startsWithIgnoreAsciiCase("private:")
             || isTrustedLocationUri(referer));
}

bool SvtSecurityOptions::isTrustedLocationUri(OUString const & uri) const {
    MutexGuard g(GetInitMutex());
    for (const auto & url : std::as_const(m_pImpl->m_seqSecureURLs))
    {
        if (UCBContentHelper::IsSubPath(url, uri))
        {
            return true;
        }
    }
    return false;
}

bool SvtSecurityOptions::isTrustedLocationUriForUpdatingLinks(
    OUString const & uri) const
{
    return GetMacroSecurityLevel() == 0 || uri.isEmpty()
        || uri.startsWithIgnoreAsciiCase("private:")
        || isTrustedLocationUri(uri);
}

sal_Int32 SvtSecurityOptions::GetMacroSecurityLevel() const
{
    MutexGuard aGuard( GetInitMutex() );
    return m_pImpl->GetMacroSecurityLevel();
}

void SvtSecurityOptions::SetMacroSecurityLevel( sal_Int32 _nLevel )
{
    MutexGuard aGuard( GetInitMutex() );
    m_pImpl->SetMacroSecurityLevel( _nLevel );
}

bool SvtSecurityOptions::IsMacroDisabled() const
{
    MutexGuard aGuard( GetInitMutex() );
    return m_pImpl->IsMacroDisabled();
}

std::vector< SvtSecurityOptions::Certificate > SvtSecurityOptions::GetTrustedAuthors() const
{
    MutexGuard aGuard( GetInitMutex() );
    return m_pImpl->GetTrustedAuthors();
}

void SvtSecurityOptions::SetTrustedAuthors( const std::vector< Certificate >& rAuthors )
{
    MutexGuard aGuard( GetInitMutex() );
    m_pImpl->SetTrustedAuthors( rAuthors );
}

bool SvtSecurityOptions::IsOptionSet( EOption eOption ) const
{
    MutexGuard aGuard( GetInitMutex() );
    return m_pImpl->IsOptionSet( eOption );
}

void SvtSecurityOptions::SetOption( EOption eOption, bool bValue )
{
    MutexGuard aGuard( GetInitMutex() );
    m_pImpl->SetOption( eOption, bValue );
}

bool SvtSecurityOptions::IsOptionEnabled( EOption eOption ) const
{
    MutexGuard aGuard( GetInitMutex() );
    return m_pImpl->IsOptionEnabled( eOption );
}

namespace
{
    class theSecurityOptionsMutex : public rtl::Static<osl::Mutex, theSecurityOptionsMutex>{};
}

Mutex& SvtSecurityOptions::GetInitMutex()
{
    return theSecurityOptionsMutex::get();
}

// map personal info strings to 1, 2, ... to remove personal info
size_t SvtSecurityMapPersonalInfo::GetInfoID( const OUString sPersonalInfo )
{
    SvtSecurityMapPersonalInfoType::iterator aIter = aInfoIDs.find( sPersonalInfo );
    if ( aIter == aInfoIDs.end() )
    {
        size_t nNewID = aInfoIDs.size() + 1;
        aInfoIDs[sPersonalInfo] = nNewID;
        return nNewID;
    }

    return aIter->second;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
