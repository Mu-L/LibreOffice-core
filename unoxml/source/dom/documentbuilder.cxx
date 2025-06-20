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

#include "documentbuilder.hxx"

#include <string.h>

#include <libxml/xmlerror.h>
#include <libxml/parser.h>

#include <memory>

#include <sal/log.hxx>
#include <comphelper/diagnose_ex.hxx>

#include <comphelper/processfactory.hxx>
#include <cppuhelper/implbase.hxx>
#include <cppuhelper/supportsservice.hxx>

#include <com/sun/star/xml/sax/SAXParseException.hpp>
#include <com/sun/star/ucb/XCommandEnvironment.hpp>
#include <com/sun/star/task/XInteractionHandler.hpp>
#include <com/sun/star/ucb/SimpleFileAccess.hpp>

#include <ucbhelper/content.hxx>
#include <ucbhelper/commandenvironment.hxx>

#include "document.hxx"

using namespace css::io;
using namespace css::lang;
using namespace css::ucb;
using namespace css::uno;
using namespace css::xml::dom;
using namespace css::xml::sax;
using namespace ucbhelper;
using css::task::XInteractionHandler;
using css::xml::sax::InputSource;


namespace DOM
{
    namespace {

    class CDefaultEntityResolver : public cppu::WeakImplHelper< XEntityResolver >
    {
    public:
        virtual InputSource SAL_CALL resolveEntity( const OUString& sPublicId, const OUString& sSystemId ) override
        {
            InputSource is;
            is.sPublicId = sPublicId;
            is.sSystemId = sSystemId;
            is.sEncoding.clear();

            try {
                Reference< XCommandEnvironment > aEnvironment(
                    new CommandEnvironment(Reference< XInteractionHandler >(),
                                           Reference< XProgressHandler >() ));
                Content aContent(sSystemId, aEnvironment, comphelper::getProcessComponentContext());

                is.aInputStream = aContent.openStream();
            } catch (const css::uno::Exception&) {
                TOOLS_WARN_EXCEPTION( "unoxml", "exception in default entity resolver");
                is.aInputStream.clear();
            }
            return is;
        }

    };

    }

    CDocumentBuilder::CDocumentBuilder()
        : m_xEntityResolver(new CDefaultEntityResolver)
    {
        // init libxml. libxml will protect itself against multiple
        // initializations so there is no problem here if this gets
        // called multiple times.
        xmlInitParser();
    }

    Sequence< OUString > SAL_CALL CDocumentBuilder::getSupportedServiceNames()
    {
        return { u"com.sun.star.xml.dom.DocumentBuilder"_ustr };
    }

    OUString SAL_CALL CDocumentBuilder::getImplementationName()
    {
        return u"com.sun.star.comp.xml.dom.DocumentBuilder"_ustr;
    }

    sal_Bool SAL_CALL CDocumentBuilder::supportsService(const OUString& aServiceName)
    {
        return cppu::supportsService(this, aServiceName);
    }

    Reference< XDOMImplementation > SAL_CALL CDocumentBuilder::getDOMImplementation()
    {

        return Reference< XDOMImplementation >();
    }

    sal_Bool SAL_CALL CDocumentBuilder::isNamespaceAware()
    {
        return true;
    }

    sal_Bool SAL_CALL CDocumentBuilder::isValidating()
    {
        return false;
    }

    Reference< XDocument > SAL_CALL CDocumentBuilder::newDocument()
    {
        std::scoped_lock const g(m_Mutex);

        // create a new document
        xmlDocPtr pDocument = xmlNewDoc(reinterpret_cast<const xmlChar*>("1.0"));
        return CDocument::CreateCDocument(pDocument);
    }

    static OUString make_error_message(xmlParserCtxtPtr ctxt)
    {
        const xmlError* lastError = xmlCtxtGetLastError(ctxt);
        return OUString(lastError->message, strlen(lastError->message), RTL_TEXTENCODING_ASCII_US) +
               "Line: " +
               OUString::number(static_cast<sal_Int32>(lastError->line)) +
               "\nColumn: " +
               OUString::number(static_cast<sal_Int32>(lastError->int2));
    }

    // -- callbacks and context struct for parsing from stream
    // -- c-linkage, so the callbacks can be used by libxml
    extern "C" {

    namespace {

    // context struct passed to IO functions
    typedef struct context {
        Reference< XInputStream > rInputStream;
        bool close;
        bool freeOnClose;
    } context_t;

    }

    static int xmlIO_read_func( void *context, char *buffer, int len)
    {
        // get the context...
        context_t *pctx = static_cast<context_t*>(context);
        if (!pctx->rInputStream.is())
            return -1;
        try {
            // try to read the requested number of bytes
            Sequence< sal_Int8 > chunk(len);
            int nread = pctx->rInputStream->readBytes(chunk, len);

            // copy bytes to the provided buffer
            memcpy(buffer, chunk.getConstArray(), nread);
            return nread;
        } catch (const css::uno::Exception&) {
            TOOLS_WARN_EXCEPTION( "unoxml", "");
            return -1;
        }
    }

    static int xmlIO_close_func(void* context)
    {
        // get the context...
        context_t *pctx = static_cast<context_t*>(context);
        if (!pctx->rInputStream.is())
            return 0;
        try
        {
            if (pctx->close)
                pctx->rInputStream->closeInput();
            if (pctx->freeOnClose)
                delete pctx;
            return 0;
        } catch (const css::uno::Exception&) {
            TOOLS_WARN_EXCEPTION( "unoxml", "");
            return -1;
        }
    }

    static xmlParserInputPtr resolve_func(void *ctx,
                                const xmlChar *publicId,
                                const xmlChar *systemId)
    {
        // get the CDocumentBuilder object
        xmlParserCtxtPtr ctxt = static_cast<xmlParserCtxtPtr>(ctx);
        CDocumentBuilder *builder = static_cast< CDocumentBuilder* >(ctxt->_private);
        Reference< XEntityResolver > resolver = builder->getEntityResolver();
        OUString sysid;
        if (systemId != nullptr)
            sysid = OUString(reinterpret_cast<char const *>(systemId), strlen(reinterpret_cast<char const *>(systemId)), RTL_TEXTENCODING_UTF8);
        OUString pubid;
        if (publicId != nullptr)
            pubid = OUString(reinterpret_cast<char const *>(publicId), strlen(reinterpret_cast<char const *>(publicId)), RTL_TEXTENCODING_UTF8);

        // resolve the entity
        InputSource src = resolver->resolveEntity(pubid, sysid);

        // create IO context on heap because this call will no longer be on the stack
        // when IO is actually performed through the callbacks. The close function must
        // free the memory which is indicated by the freeOnClose field in the context struct
        context_t *c = new context_t;
        c->rInputStream = src.aInputStream;
        c->close = true;
        c->freeOnClose = true;

        // set up the inputBuffer and inputPtr for libxml
        xmlParserInputBufferPtr pBuffer =
            xmlParserInputBufferCreateIO(xmlIO_read_func, xmlIO_close_func, c, XML_CHAR_ENCODING_NONE);
        xmlParserInputPtr pInput =
                    xmlNewIOInputStream(ctxt, pBuffer, XML_CHAR_ENCODING_NONE);
        return pInput;
    }

#if 0
    static xmlParserInputPtr external_entity_loader(const char *URL, const char * /*ID*/, xmlParserCtxtPtr ctxt)
    {
        // just call our resolver function using the URL as systemId
        return resolve_func(ctxt, 0, (const xmlChar*)URL);
    }
#endif

    // default warning handler does not trigger assertion
    static void warning_func(void * ctx, const char * /*msg*/, ...)
    {
        try
        {
            xmlParserCtxtPtr const pctx = static_cast<xmlParserCtxtPtr>(ctx);

            SAL_INFO(
                "unoxml",
                "libxml2 warning: "
                << make_error_message(pctx));

            CDocumentBuilder * const pDocBuilder = static_cast<CDocumentBuilder*>(pctx->_private);

            Reference<XErrorHandler> xErrorHandler = pDocBuilder->getErrorHandler();
            if (xErrorHandler.is())   // if custom error handler is set (using setErrorHandler ())
            {
                // Prepare SAXParseException to be passed to custom XErrorHandler::warning function
                const xmlError* lastError = xmlCtxtGetLastError(pctx);
                css::xml::sax::SAXParseException saxex(make_error_message(pctx), {}, {}, {}, {},
                                                       lastError->line, lastError->int2);

                // Call custom warning function
                xErrorHandler->warning(::css::uno::Any(saxex));
            }
        }
        catch (const css::uno::Exception &)
        {
            // Protect lib2xml from UNO Exception
            TOOLS_WARN_EXCEPTION("unoxml", "DOM::warning_func");
        }
    }

    // default error handler triggers assertion
    static void error_func(void * ctx, const char * /*msg*/, ...)
    {
        try
        {
            xmlParserCtxtPtr const pctx = static_cast<xmlParserCtxtPtr>(ctx);
            SAL_WARN(
                "unoxml",
                "libxml2 error: "
                << make_error_message(pctx));

            CDocumentBuilder * const pDocBuilder = static_cast<CDocumentBuilder*>(pctx->_private);

            Reference<XErrorHandler> xErrorHandler = pDocBuilder->getErrorHandler();
            if (xErrorHandler.is())   // if custom error handler is set (using setErrorHandler ())
            {
                // Prepare SAXParseException to be passed to custom XErrorHandler::error function
                const xmlError* lastError = xmlCtxtGetLastError(pctx);
                css::xml::sax::SAXParseException saxex(make_error_message(pctx), {}, {}, {}, {},
                                                       lastError->line, lastError->int2);

                // Call custom warning function
                xErrorHandler->error(::css::uno::Any(saxex));
            }
        }
        catch (const css::uno::Exception &)
        {
            // Protect lib2xml from UNO Exception
            TOOLS_WARN_EXCEPTION("unoxml", "DOM::error_func");
        }
    }
    } // extern "C"

    static void throwEx(xmlParserCtxtPtr ctxt)
    {
        const xmlError* lastError = xmlCtxtGetLastError(ctxt);
        css::xml::sax::SAXParseException saxex(make_error_message(ctxt), {}, {}, {}, {},
                                               lastError->line, lastError->int2);
        throw saxex;
    }

    namespace {

    struct XmlFreeParserCtxt {
        void operator ()(xmlParserCtxt * p) const { xmlFreeParserCtxt(p); }
    };

    }

    Reference< XDocument > SAL_CALL CDocumentBuilder::parse(const Reference< XInputStream >& is)
    {
        if (!is.is()) {
            throw RuntimeException();
        }

        std::scoped_lock const g(m_Mutex);

        // IO context struct.  Must outlive pContext, as destroying that via
        // xmlFreeParserCtxt may still access this context_t
        context_t c;
        c.rInputStream = is;
        // we did not open the stream, thus we do not close it.
        c.close = false;
        c.freeOnClose = false;

        std::unique_ptr<xmlParserCtxt, XmlFreeParserCtxt> const pContext(
                xmlNewParserCtxt());

        // register error functions to prevent errors being printed
        // on the console
        pContext->_private = this;
        pContext->sax->error = error_func;
        pContext->sax->warning = warning_func;
        pContext->sax->resolveEntity = resolve_func;

        xmlDocPtr const pDoc = xmlCtxtReadIO(pContext.get(),
                xmlIO_read_func, xmlIO_close_func, &c, nullptr, nullptr, 0);

        if (pDoc == nullptr) {
            throwEx(pContext.get());
        }
        return CDocument::CreateCDocument(pDoc);
    }

    Reference< XDocument > SAL_CALL CDocumentBuilder::parseURI(const OUString& sUri)
    {
        std::scoped_lock const g(m_Mutex);

        std::unique_ptr<xmlParserCtxt, XmlFreeParserCtxt> const pContext(
                xmlNewParserCtxt());
        pContext->_private = this;
        pContext->sax->error = error_func;
        pContext->sax->warning = warning_func;
        pContext->sax->resolveEntity = resolve_func;
        // xmlSetExternalEntityLoader(external_entity_loader);
        OString oUri = OUStringToOString(sUri, RTL_TEXTENCODING_UTF8);
        char *uri = const_cast<char*>(oUri.getStr());
        xmlDocPtr pDoc = xmlCtxtReadFile(pContext.get(), uri, nullptr, 0);

        Reference< XDocument > xRet;

        // if we failed to parse the URI as a simple file, let's try via a ucb stream.
        // For Android file:///assets/ URLs which must go via the osl/ file API.
        if (pDoc == nullptr) {
            Reference < XSimpleFileAccess3 > xStreamAccess(
                SimpleFileAccess::create( comphelper::getProcessComponentContext() ) );
            Reference< XInputStream > xInStream = xStreamAccess->openFileRead( sUri );
            if (!xInStream.is())
                throwEx(pContext.get());

            // loop over every layout entry in current file
            xRet = parse( xInStream );

            xInStream->closeInput();
            xInStream.clear();

        } else
            xRet = CDocument::CreateCDocument(pDoc).get();

        return xRet;
    }

    void SAL_CALL
    CDocumentBuilder::setEntityResolver(Reference< XEntityResolver > const& xER)
    {
        std::scoped_lock const g(m_Mutex);

        m_xEntityResolver = xER;
    }

    Reference< XEntityResolver > CDocumentBuilder::getEntityResolver()
    {
        std::scoped_lock const g(m_Mutex);

        return m_xEntityResolver;
    }

    void SAL_CALL
    CDocumentBuilder::setErrorHandler(Reference< XErrorHandler > const& xEH)
    {
        std::scoped_lock const g(m_Mutex);

        m_xErrorHandler = xEH;
    }

    Reference< XErrorHandler > CDocumentBuilder::getErrorHandler()
    {
        std::scoped_lock const g(m_Mutex);

        return m_xErrorHandler;
    }
}

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface*
unoxml_CDocumentBuilder_get_implementation(
    css::uno::XComponentContext* , css::uno::Sequence<css::uno::Any> const&)
{
    return cppu::acquire(new DOM::CDocumentBuilder());
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
