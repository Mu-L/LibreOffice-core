/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <set>
#include <string_view>

#include <boost/property_tree/ptree.hpp>
#include <boost/variant.hpp>
#include <boost/container/flat_map.hpp>

#include <osl/thread.h>
#include <rtl/ref.hxx>
#include <rtl/strbuf.hxx>
#include <vcl/idle.hxx>
#include <LibreOfficeKit/LibreOfficeKit.h>
#include <LibreOfficeKit/LibreOfficeKitEnums.h>
#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/lang/XComponent.hpp>
#include <tools/gen.hxx>
#include <sfx2/lokcallback.hxx>
#include <sfx2/lokhelper.hxx>

#include <desktop/dllapi.h>

class LOKInteractionHandler;

namespace desktop {

    /// Represents an invalidated rectangle inside a given document part.
    struct RectangleAndPart
    {
        tools::Rectangle m_aRectangle;
        int m_nPart;
        int m_nMode;

        // This is the "EMPTY" rectangle, which somewhat confusingly actually means
        // to drop all rectangles (see LOK_CALLBACK_INVALIDATE_TILES documentation),
        // and so it is actually an infinite rectangle and not an empty one.
        constexpr static tools::Rectangle emptyAllRectangle = {0, 0, SfxLokHelper::MaxTwips, SfxLokHelper::MaxTwips};

        RectangleAndPart()
            : m_nPart(INT_MIN)  // -1 is reserved to mean "all parts".
            , m_nMode(0)
        {
        }

        RectangleAndPart(const tools::Rectangle* pRect, int nPart, int nMode)
            : m_aRectangle( pRect ? SanitizedRectangle(*pRect) : emptyAllRectangle)
            , m_nPart(nPart)
            , m_nMode(nMode)
        {
        }

        OString toString() const
        {
            if (m_nPart >= -1)
                return (isInfinite() ? "EMPTY"_ostr : m_aRectangle.toString())
                    + ", " + OString::number(m_nPart) + ", " + OString::number(m_nMode);
            else
                return (isInfinite() ? "EMPTY"_ostr : m_aRectangle.toString());
        }

        /// Infinite Rectangle is both sides are
        /// equal or longer than SfxLokHelper::MaxTwips.
        bool isInfinite() const
        {
            return m_aRectangle.GetWidth() >= SfxLokHelper::MaxTwips &&
                   m_aRectangle.GetHeight() >= SfxLokHelper::MaxTwips;
        }

        /// Empty Rectangle is when it has zero dimensions.
        bool isEmpty() const
        {
            return m_aRectangle.IsEmpty();
        }

        static RectangleAndPart Create(const OString& rPayload);
        /// Makes sure a rectangle is valid (apparently some code does not like negative coordinates for example).
        static tools::Rectangle SanitizedRectangle(tools::Long nLeft, tools::Long nTop, tools::Long nWidth, tools::Long nHeight);
        static tools::Rectangle SanitizedRectangle(const tools::Rectangle& rect);
    };

    /// One instance of this per view, handles flushing callbacks
    class SAL_DLLPUBLIC_RTTI CallbackFlushHandler final : public SfxLokCallbackInterface
    {
    public:
        DESKTOP_DLLPUBLIC explicit CallbackFlushHandler(LibreOfficeKitDocument* pDocument, LibreOfficeKitCallback pCallback, void* pData);
        DESKTOP_DLLPUBLIC virtual ~CallbackFlushHandler() override;
        // TODO This should be dropped and the binary libreOfficeKitViewCallback() variants should be called?
        DESKTOP_DLLPUBLIC void queue(const int type, const OString& data);

        /// Disables callbacks on this handler. Must match with identical count
        /// of enableCallbacks. Used during painting and changing views.
        void disableCallbacks() { ++m_nDisableCallbacks; }
        /// Enables callbacks on this handler. Must match with identical count
        /// of disableCallbacks. Used during painting and changing views.
        void enableCallbacks() { --m_nDisableCallbacks; }
        /// Returns true iff callbacks are disabled.
        bool callbacksDisabled() const { return m_nDisableCallbacks != 0; }

        void addViewStates(int viewId);
        void removeViewStates(int viewId);

        void setViewId( int viewId ) { m_viewId = viewId; }

        DESKTOP_DLLPUBLIC void tilePainted(int nPart, int nMode, const tools::Rectangle& rRectangle);
        const OString& getViewRenderState() const { return m_aViewRenderState; }
        const std::map<int, std::map<int, tools::Rectangle>>& getPaintedTiles() const
        {
            return m_aPaintedTiles;
        }
        void setPaintedTiles(const std::map<int, std::map<int, tools::Rectangle>>& rPaintedTiles)
        {
            m_aPaintedTiles = rPaintedTiles;
        }

        // SfxLockCallbackInterface
        virtual void libreOfficeKitViewCallback(int nType, const OString& pPayload) override;
        virtual void libreOfficeKitViewCallbackWithViewId(int nType, const OString& pPayload, int nViewId) override;
        DESKTOP_DLLPUBLIC virtual void libreOfficeKitViewInvalidateTilesCallback(const tools::Rectangle* pRect, int nPart, int nMode) override;
        virtual void libreOfficeKitViewUpdatedCallback(int nType) override;
        virtual void libreOfficeKitViewUpdatedCallbackPerViewId(int nType, int nViewId, int nSourceViewId) override;
        virtual void libreOfficeKitViewAddPendingInvalidateTiles() override;
        virtual void dumpState(rtl::OStringBuffer &rState) override;

    private:
        struct CallbackData
        {
            CallbackData(OString payload)
                : PayloadString(std::move(payload))
            {
            }

            CallbackData(OString payload, int viewId)
                : PayloadString(std::move(payload))
                , PayloadObject(viewId)
            {
            }

            CallbackData(const tools::Rectangle* pRect, int viewId)
                : PayloadObject(RectangleAndPart(pRect, viewId, 0))
            { // PayloadString will be done on demand
            }

            CallbackData(const tools::Rectangle* pRect, int part, int mode)
                : PayloadObject(RectangleAndPart(pRect, part, mode))
            { // PayloadString will be done on demand
            }

            const OString& getPayload() const;
            /// Update a RectangleAndPart object and update PayloadString if necessary.
            void updateRectangleAndPart(const RectangleAndPart& rRectAndPart);
            /// Return the parsed RectangleAndPart instance.
            const RectangleAndPart& getRectangleAndPart() const;
            /// Parse and set the JSON object and return it. Clobbers PayloadString.
            boost::property_tree::ptree& setJson(const std::string& payload);
            /// Set a Json object and update PayloadString.
            void setJson(const boost::property_tree::ptree& rTree);
            /// Return the parsed JSON instance.
            const boost::property_tree::ptree& getJson() const;

            int getViewId() const;

            bool isEmpty() const
            {
                return PayloadString.isEmpty() && PayloadObject.which() == 0;
            }
            void clear()
            {
                PayloadString.clear();
                PayloadObject = boost::blank();
            }

            /// Validate that the payload and parsed object match.
            bool validate() const;

            /// Returns true iff there is cached data.
            bool isCached() const { return PayloadObject.which() != 0; }

        private:
            mutable OString PayloadString;

            /// The parsed payload cache. Update validate() when changing this.
            mutable boost::variant<boost::blank, RectangleAndPart, boost::property_tree::ptree, int> PayloadObject;
        };

        typedef std::vector<int> queue_type1;
        typedef std::vector<CallbackData> queue_type2;

        void scheduleFlush();
        void invoke();
        bool removeAll(int type);
        bool removeAll(int type, const std::function<bool (const CallbackData&)>& rTestFunc);
        bool processInvalidateTilesEvent(int type, CallbackData& aCallbackData);
        bool processWindowEvent(int type, CallbackData& aCallbackData);
        queue_type2::iterator toQueue2(queue_type1::iterator);
        queue_type2::reverse_iterator toQueue2(queue_type1::reverse_iterator);
        void queue(const int type, CallbackData& data);
        void enqueueUpdatedTypes();
        void enqueueUpdatedType( int type, const SfxViewShell* sourceViewShell, int viewId );

        void stop();

        /** we frequently want to scan the queue, and mostly when we do so, we only care about the element type
            so we split the queue in 2 to make the scanning cache friendly. */
        queue_type1 m_queue1;
        queue_type2 m_queue2;
        std::map<int, OString> m_states;
        std::unordered_map<OString, OString> m_lastStateChange;
        std::unordered_map<int, std::unordered_map<int, OString>> m_viewStates;

        /// BBox of already painted tiles: part number -> part mode -> rectangle.
        std::map<int, std::map<int, tools::Rectangle>> m_aPaintedTiles;

        // For some types only the last message matters (see isUpdatedType()) or only the last message
        // per each viewId value matters (see isUpdatedTypePerViewId()), so instead of using push model
        // where we'd get flooded by repeated messages (which might be costly to generate and process),
        // the preferred way is that libreOfficeKitViewUpdatedCallback()
        // or libreOfficeKitViewUpdatedCallbackPerViewId() get called to notify about such a message being
        // needed, and we'll set a flag here to fetch the actual message before flushing.
        void setUpdatedType( int nType, bool value );
        void setUpdatedTypePerViewId( int nType, int nViewId, int nSourceViewId, bool value );
        void resetUpdatedType( int nType);
        void resetUpdatedTypePerViewId( int nType, int nViewId );
        std::vector<bool> m_updatedTypes; // index is type, value is if set
        struct PerViewIdData
        {
            bool set = false; // value is if set
            int sourceViewId;
        };
        // Flat_map is used in preference to unordered_map because the map is accessed very often.
        boost::container::flat_map<int, std::vector<PerViewIdData>> m_updatedTypesPerViewId; // key is view, index is type

        LibreOfficeKitDocument* m_pDocument;
        OString m_aViewRenderState;
        int m_viewId = -1; // view id of the associated SfxViewShell
        LibreOfficeKitCallback m_pCallback;
        ImplSVEvent* m_pFlushEvent;
        void *m_pData;
        int m_nDisableCallbacks;
        std::recursive_mutex m_mutex;

        DECL_LINK(FlushQueue, void*, void);
    };

    struct DESKTOP_DLLPUBLIC LibLODocument_Impl : public _LibreOfficeKitDocument
    {
        css::uno::Reference<css::lang::XComponent> mxComponent;
        std::shared_ptr< LibreOfficeKitDocumentClass > m_pDocumentClass;
        std::map<size_t, std::shared_ptr<CallbackFlushHandler>> mpCallbackFlushHandlers;
        const int mnDocumentId;
        std::set<OUString> maFontsMissing;

        explicit LibLODocument_Impl(css::uno::Reference<css::lang::XComponent> xComponent,
                                    int nDocumentId);
        ~LibLODocument_Impl();

        void updateViewsForPaintedTile(int nOrigViewId, int nPart, int nMode, const tools::Rectangle& rRectangle);
    };

    struct DESKTOP_DLLPUBLIC LibLibreOffice_Impl : public _LibreOfficeKit
    {
        OUString maLastExceptionMsg;
        std::shared_ptr< LibreOfficeKitClass > m_pOfficeClass;
        oslThread maThread;
        LibreOfficeKitCallback mpCallback;
        void *mpCallbackData;
        int64_t mOptionalFeatures;
        std::map<OString, rtl::Reference<LOKInteractionHandler>> mInteractionMap;

        LibLibreOffice_Impl();
        ~LibLibreOffice_Impl();

        bool hasOptionalFeature(LibreOfficeKitOptionalFeatures const feature)
        {
            return (mOptionalFeatures & feature) != 0;
        }

        void dumpState(rtl::OStringBuffer &aState);
    };

    /// Helper function to extract the value from parameters delimited by
    /// comma, like: Name1=Value1,Name2=Value2,Name3=Value3.
    /// @param rOptions When extracted, the Param=Value is removed from it.
    DESKTOP_DLLPUBLIC OUString extractParameter(OUString& aOptions, std::u16string_view rName);

    /// Helper function to convert JSON to a vector of PropertyValues.
    /// Public to be unit-test-able.
    DESKTOP_DLLPUBLIC std::vector<css::beans::PropertyValue> jsonToPropertyValuesVector(const char* pJSON);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
