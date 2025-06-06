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
#ifndef INCLUDED_SW_SOURCE_FILTER_XML_WRTXML_HXX
#define INCLUDED_SW_SOURCE_FILTER_XML_WRTXML_HXX

#include <com/sun/star/io/XOutputStream.hpp>
#include <com/sun/star/uno/Sequence.hxx>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <com/sun/star/beans/PropertyValue.hpp>
#include <shellio.hxx>

#include <vector>

namespace com::sun::star {
    namespace uno { template<class A> class Reference; }
    namespace uno { template<class A> class Sequence; }
    namespace uno { class Any; }
    namespace lang { class XComponent; }
    namespace beans { struct PropertyValue; }
}

class SwXMLWriter : public StgWriter
{
    ErrCodeMsg Write_(const SfxItemSet* pMediumItemSet);

    using StgWriter::Write;

protected:
    virtual ErrCodeMsg WriteStorage() override;
    virtual ErrCodeMsg WriteMedium( SfxMedium& aTargetMedium ) override;

public:

    SwXMLWriter( const OUString& rBaseURL );
    virtual ~SwXMLWriter() override;

    virtual ErrCodeMsg Write( SwPaM&, SfxMedium&, const OUString* ) override;

private:
    std::unordered_map<OString, OUString> maEmbeddedFontFiles;

    // helper methods to write XML streams

    // write a single XML stream into the package
    bool WriteThroughComponent(
        // the component we export
        const css::uno::Reference<css::lang::XComponent> & xComponent,
        const char* pStreamName,        // the stream name
        // service factory for pServiceName
        const css::uno::Reference<css::uno::XComponentContext> & rFactory,
        const char* pServiceName,       // service name of the component
        // the argument (XInitialization)
        const css::uno::Sequence<css::uno::Any> & rArguments,
        // output descriptor
        const css::uno::Sequence<css::beans::PropertyValue> & rMediaDesc );

    // write a single output stream
    // (to be called either directly or by WriteThroughComponent(...))
    bool WriteThroughComponent(
        const css::uno::Reference<css::io::XOutputStream> & xOutputStream,
        const css::uno::Reference<css::lang::XComponent> & xComponent,
        const css::uno::Reference<css::uno::XComponentContext> & rFactory,
        const char* pServiceName,
        const css::uno::Sequence<css::uno::Any> & rArguments,
        const css::uno::Sequence<css::beans::PropertyValue> & rMediaDesc );
};

#endif // INCLUDED_SW_SOURCE_FILTER_XML_WRTXML_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
