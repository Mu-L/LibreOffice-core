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

#include "address.hxx"
#include <editeng/unotext.hxx>
#include <svl/lstner.hxx>
#include <com/sun/star/text/XTextFieldsSupplier.hpp>
#include <com/sun/star/sheet/XHeaderFooterContent.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <comphelper/servicehelper.hxx>
#include <cppuhelper/implbase.hxx>
#include <cppuhelper/weakref.hxx>
#include <unotools/weakref.hxx>

#include <rtl/ref.hxx>

#include <memory>

class EditEngine;
class EditTextObject;
class SvxEditEngineForwarder;
class ScDocShell;
class ScCellObj;
class ScSimpleEditSource;
class ScCellEditSource;
class ScEditEngineDefaulter;
class ScFieldEditEngine;
class ScHeaderFooterTextObj;

struct ScHeaderFieldData;

enum class ScHeaderFooterPart{ LEFT, CENTER, RIGHT };

//  ScHeaderFooterContentObj is a dumb container which must be re-written into
//  the page template using setPropertyValue

class ScHeaderFooterContentObj final : public cppu::WeakImplHelper<
                            css::sheet::XHeaderFooterContent,
                            css::lang::XServiceInfo >
{
private:
    rtl::Reference<ScHeaderFooterTextObj> mxLeftText;
    rtl::Reference<ScHeaderFooterTextObj> mxCenterText;
    rtl::Reference<ScHeaderFooterTextObj> mxRightText;

public:
                            ScHeaderFooterContentObj();
    virtual                 ~ScHeaderFooterContentObj() override;

                            // for ScPageHFItem (using getImplementation)
    const EditTextObject* GetLeftEditObject() const;
    const EditTextObject* GetCenterEditObject() const;
    const EditTextObject* GetRightEditObject() const;

    void Init( const EditTextObject* pLeft,
                              const EditTextObject* pCenter,
                              const EditTextObject* pRight);

                            // XHeaderFooterContent
    virtual css::uno::Reference< css::text::XText > SAL_CALL
                            getLeftText() override;
    virtual css::uno::Reference< css::text::XText > SAL_CALL
                            getCenterText() override;
    virtual css::uno::Reference< css::text::XText > SAL_CALL
                            getRightText() override;

    static rtl::Reference<ScHeaderFooterContentObj> getImplementation(const css::uno::Reference<css::sheet::XHeaderFooterContent>& rObj);

                            // XServiceInfo
    virtual OUString SAL_CALL getImplementationName() override;
    virtual sal_Bool SAL_CALL supportsService( const OUString& ServiceName ) override;
    virtual css::uno::Sequence< OUString > SAL_CALL getSupportedServiceNames() override;

};

//  ScHeaderFooterTextData: shared data between sub objects of a ScHeaderFooterTextObj

class ScHeaderFooterTextData
{
private:
    std::unique_ptr<EditTextObject> mpTextObj;
    unotools::WeakReference<ScHeaderFooterContentObj> xContentObj;
    ScHeaderFooterPart          nPart;
    std::unique_ptr<ScEditEngineDefaulter>  pEditEngine;
    std::unique_ptr<SvxEditEngineForwarder> pForwarder;
    bool                        bDataValid;

public:
    ScHeaderFooterTextData(const ScHeaderFooterTextData&) = delete;
    const ScHeaderFooterTextData& operator=(const ScHeaderFooterTextData&) = delete;
    ScHeaderFooterTextData(
        unotools::WeakReference<ScHeaderFooterContentObj> xContent, ScHeaderFooterPart nP, const EditTextObject* pTextObj);
    ~ScHeaderFooterTextData();

                            // helper functions
    SvxTextForwarder*       GetTextForwarder();
    void UpdateData();
    void UpdateData(EditEngine& rEditEngine);
    ScEditEngineDefaulter*  GetEditEngine() { GetTextForwarder(); return pEditEngine.get(); }

    ScHeaderFooterPart      GetPart() const         { return nPart; }
    rtl::Reference<ScHeaderFooterContentObj> GetContentObj() const { return xContentObj; }

    const EditTextObject* GetTextObject() const { return mpTextObj.get(); }
};

/**
 * Each of these instances represent, the left, center or right part of the
 * header of footer of a page.
 *
 * ScHeaderFooterTextObj changes the text in a ScHeaderFooterContentObj.
 */
class ScHeaderFooterTextObj final : public cppu::WeakImplHelper<
                            css::text::XText,
                            css::text::XTextRangeMover,
                            css::container::XEnumerationAccess,
                            css::text::XTextFieldsSupplier,
                            css::lang::XServiceInfo >
{
private:
    ScHeaderFooterTextData      aTextData;
    rtl::Reference<SvxUnoText> mxUnoText;

    void                    CreateUnoText_Impl();

public:
    ScHeaderFooterTextObj(
        const unotools::WeakReference<ScHeaderFooterContentObj>& xContent, ScHeaderFooterPart nP, const EditTextObject* pTextObj);
    virtual ~ScHeaderFooterTextObj() override;

    const EditTextObject* GetTextObject() const;
    const SvxUnoText&       GetUnoText();

    static void             FillDummyFieldData( ScHeaderFieldData& rData );

                            // XText
    virtual void SAL_CALL   insertTextContent( const css::uno::Reference< css::text::XTextRange >& xRange,
                                const css::uno::Reference< css::text::XTextContent >& xContent,
                                sal_Bool bAbsorb ) override;
    virtual void SAL_CALL   removeTextContent( const css::uno::Reference< css::text::XTextContent >& xContent ) override;

                            // XSimpleText
    virtual css::uno::Reference< css::text::XTextCursor > SAL_CALL
                            createTextCursor() override;
    virtual css::uno::Reference< css::text::XTextCursor > SAL_CALL
                            createTextCursorByRange( const css::uno::Reference< css::text::XTextRange >& aTextPosition ) override;
    virtual void SAL_CALL   insertString( const css::uno::Reference< css::text::XTextRange >& xRange,
                                        const OUString& aString, sal_Bool bAbsorb ) override;
    virtual void SAL_CALL   insertControlCharacter( const css::uno::Reference< css::text::XTextRange >& xRange,
                                        sal_Int16 nControlCharacter, sal_Bool bAbsorb ) override;

                            // XTextRange
    virtual css::uno::Reference< css::text::XText > SAL_CALL
                            getText() override;
    virtual css::uno::Reference< css::text::XTextRange > SAL_CALL
                            getStart() override;
    virtual css::uno::Reference< css::text::XTextRange > SAL_CALL
                            getEnd() override;
    virtual OUString SAL_CALL getString() override;
    virtual void SAL_CALL   setString( const OUString& aString ) override;

                            // XTextRangeMover
    virtual void SAL_CALL   moveTextRange( const css::uno::Reference< css::text::XTextRange >& xRange,
                                        sal_Int16 nParagraphs ) override;

                            // XEnumerationAccess
    virtual css::uno::Reference< css::container::XEnumeration > SAL_CALL
                            createEnumeration() override;

                            // XElementAccess
    virtual css::uno::Type SAL_CALL getElementType() override;
    virtual sal_Bool SAL_CALL hasElements() override;

                            // XTextFieldsSupplier
    virtual css::uno::Reference< css::container::XEnumerationAccess > SAL_CALL
                            getTextFields() override;
    virtual css::uno::Reference< css::container::XNameAccess > SAL_CALL
                            getTextFieldMasters() override;

                            // XServiceInfo
    virtual OUString SAL_CALL getImplementationName() override;
    virtual sal_Bool SAL_CALL supportsService( const OUString& ServiceName ) override;
    virtual css::uno::Sequence< OUString > SAL_CALL getSupportedServiceNames() override;

};

//  derived cursor objects for getImplementation and getText/getStart/getEnd

//! uno3: SvxUnoTextCursor is not derived from XUnoTunnel, but should be (?)

class ScCellTextCursor final : public SvxUnoTextCursor
{
    rtl::Reference<ScCellObj> mxTextObj;

public:
                            ScCellTextCursor(ScCellObj& rText);
        virtual             ~ScCellTextCursor() noexcept override;

    ScCellTextCursor(ScCellTextCursor const &) = default;
    ScCellTextCursor(ScCellTextCursor &&) = default;
    ScCellTextCursor & operator =(ScCellTextCursor const &) = delete; // due to SvxUnoTextCursor
    ScCellTextCursor & operator =(ScCellTextCursor &&) = delete; // due to SvxUnoTextCursor

    ScCellObj&              GetCellObj() const  { return *mxTextObj; }

                            // SvxUnoTextCursor methods reimplemented here:
    virtual css::uno::Reference< css::text::XText > SAL_CALL
                            getText() override;
    virtual css::uno::Reference< css::text::XTextRange > SAL_CALL
                            getStart() override;
    virtual css::uno::Reference< css::text::XTextRange > SAL_CALL
                            getEnd() override;

                            // XUnoTunnel
    UNO3_GETIMPLEMENTATION_DECL(ScCellTextCursor)
};

class ScHeaderFooterTextCursor final : public SvxUnoTextCursor
{
private:
    rtl::Reference<ScHeaderFooterTextObj> rTextObj;

public:
                            ScHeaderFooterTextCursor(rtl::Reference<ScHeaderFooterTextObj> const & rText);
        virtual             ~ScHeaderFooterTextCursor() noexcept override;

    ScHeaderFooterTextCursor(ScHeaderFooterTextCursor const &) = default;
    ScHeaderFooterTextCursor(ScHeaderFooterTextCursor &&) = default;
    ScHeaderFooterTextCursor & operator =(ScHeaderFooterTextCursor const &) = delete; // due to SvxUnoTextCursor
    ScHeaderFooterTextCursor & operator =(ScHeaderFooterTextCursor &&) = delete; // due to SvxUnoTextCursor

                            // SvxUnoTextCursor methods reimplemented here:
    virtual css::uno::Reference< css::text::XText > SAL_CALL
                            getText() override;
    virtual css::uno::Reference< css::text::XTextRange > SAL_CALL
                            getStart() override;
    virtual css::uno::Reference< css::text::XTextRange > SAL_CALL
                            getEnd() override;

                            // XUnoTunnel
    UNO3_GETIMPLEMENTATION_DECL(ScHeaderFooterTextCursor)
};

class ScDrawTextCursor final : public SvxUnoTextCursor
{
private:
    css::uno::Reference< css::text::XText > xParentText;

public:
                            ScDrawTextCursor( css::uno::Reference< css::text::XText > xParent,
                                            const SvxUnoTextBase& rText );
    virtual                  ~ScDrawTextCursor() noexcept override;

    ScDrawTextCursor(ScDrawTextCursor const &) = default;
    ScDrawTextCursor(ScDrawTextCursor &&) = default;
    ScDrawTextCursor & operator =(ScDrawTextCursor const &) = delete; // due to SvxUnoTextCursor
    ScDrawTextCursor & operator =(ScDrawTextCursor &&) = delete; // due to SvxUnoTextCursor

                            // SvxUnoTextCursor methods reimplemented here:
    virtual css::uno::Reference< css::text::XText > SAL_CALL
                            getText() override;
    virtual css::uno::Reference< css::text::XTextRange > SAL_CALL
                            getStart() override;
    virtual css::uno::Reference< css::text::XTextRange > SAL_CALL
                            getEnd() override;

                            // XUnoTunnel
    UNO3_GETIMPLEMENTATION_DECL(ScDrawTextCursor)
};

// ScAnnotationTextCursor isn't needed anymore - SvxUnoTextCursor is used instead

//  ScEditEngineTextObj for formatted cell content that is not inserted in a cell or header/footer
//  (used for XML export of change tracking contents)

class ScSimpleEditSourceHelper
{
    std::unique_ptr<ScEditEngineDefaulter>  pEditEngine;
    std::unique_ptr<SvxEditEngineForwarder> pForwarder;
    std::unique_ptr<ScSimpleEditSource>     pOriginalSource;

public:
            ScSimpleEditSourceHelper(SfxItemPool* pEditEnginePool);
            ~ScSimpleEditSourceHelper();

    ScSimpleEditSource* GetOriginalSource() const   { return pOriginalSource.get(); }
    ScEditEngineDefaulter* GetEditEngine() const    { return pEditEngine.get(); }
};

class ScEditEngineTextObj final : public ScSimpleEditSourceHelper, public SvxUnoText
{
public:
                        ScEditEngineTextObj(SfxItemPool* pEditEngineItemPool);
        virtual         ~ScEditEngineTextObj() noexcept override;

    void                SetText( const EditTextObject& rTextObject );
    std::unique_ptr<EditTextObject> CreateTextObject();
};

//  ScCellTextData: shared data between sub objects of a cell text object

class ScCellTextData : public SfxListener
{
protected:
    ScDocShell*             pDocShell;
    ScAddress               aCellPos;
    std::unique_ptr<ScFieldEditEngine> pEditEngine;
    std::unique_ptr<SvxEditEngineForwarder> pForwarder;
    std::unique_ptr<ScCellEditSource> pOriginalSource;
    bool                    bDataValid;
    bool                    bInUpdate;
    bool                    bDirty;
    bool                    bDoUpdate;

public:
                            ScCellTextData(ScDocShell* pDocSh, const ScAddress& rP);
    virtual                 ~ScCellTextData() override;

    virtual void            Notify( SfxBroadcaster& rBC, const SfxHint& rHint ) override;

                            // helper functions for ScSharedCellEditSource:
    virtual SvxTextForwarder* GetTextForwarder();
    void                    UpdateData();
    ScFieldEditEngine*      GetEditEngine() { GetTextForwarder(); return pEditEngine.get(); }

    ScCellEditSource* GetOriginalSource();        // used as argument for SvxUnoText ctor

                            // used for ScCellEditSource:
    ScDocShell*             GetDocShell() const     { return pDocShell; }
    const ScAddress&        GetCellPos() const      { return aCellPos; }

    bool                    IsDirty() const         { return bDirty; }
    void                    SetDoUpdate(bool bValue)    { bDoUpdate = bValue; }
};

class ScCellTextObj final : public ScCellTextData, public SvxUnoText
{
public:
                 ScCellTextObj(ScDocShell* pDocSh, const ScAddress& rP);
        virtual  ~ScCellTextObj() COVERITY_NOEXCEPT_FALSE override;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
