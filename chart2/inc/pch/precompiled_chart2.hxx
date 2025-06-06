/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/*
 This file has been autogenerated by update_pch.sh. It is possible to edit it
 manually (such as when an include file has been moved/renamed/removed). All such
 manual changes will be rewritten by the next run of update_pch.sh (which presumably
 also fixes all possible problems, so it's usually better to use it).

 Generated on 2025-03-30 09:01:07 using:
 ./bin/update_pch chart2 chart2 --cutoff=6 --exclude:system --include:module --include:local

 If after updating build fails, use the following command to locate conflicting headers:
 ./bin/update_pch_bisect ./chart2/inc/pch/precompiled_chart2.hxx "make chart2.build" --find-conflicts
*/

#include <sal/config.h>
#if PCH_LEVEL >= 1
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <float.h>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <iterator>
#include <limits.h>
#include <limits>
#include <map>
#include <math.h>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <optional>
#include <ostream>
#include <set>
#include <span>
#include <stddef.h>
#include <stdexcept>
#include <string.h>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree_fwd.hpp>
#endif // PCH_LEVEL >= 1
#if PCH_LEVEL >= 2
#include <osl/conditn.hxx>
#include <osl/diagnose.h>
#include <osl/diagnose.hxx>
#include <osl/doublecheckedlocking.h>
#include <osl/endian.h>
#include <osl/getglobalmutex.hxx>
#include <osl/interlck.h>
#include <osl/mutex.h>
#include <osl/mutex.hxx>
#include <rtl/alloc.h>
#include <rtl/character.hxx>
#include <rtl/instance.hxx>
#include <rtl/locale.h>
#include <rtl/math.h>
#include <rtl/math.hxx>
#include <rtl/ref.hxx>
#include <rtl/strbuf.h>
#include <rtl/strbuf.hxx>
#include <rtl/string.h>
#include <rtl/string.hxx>
#include <rtl/stringconcat.hxx>
#include <rtl/stringutils.hxx>
#include <rtl/textcvt.h>
#include <rtl/textenc.h>
#include <rtl/ustrbuf.hxx>
#include <rtl/ustring.h>
#include <rtl/ustring.hxx>
#include <rtl/uuid.h>
#include <sal/backtrace.hxx>
#include <sal/detail/log.h>
#include <sal/log.hxx>
#include <sal/macros.h>
#include <sal/saldllapi.h>
#include <sal/types.h>
#include <sal/typesizes.h>
#include <vcl/BinaryDataContainer.hxx>
#include <vcl/GraphicAttributes.hxx>
#include <vcl/GraphicExternalLink.hxx>
#include <vcl/GraphicObject.hxx>
#include <vcl/IDialogRenderable.hxx>
#include <vcl/Scanline.hxx>
#include <vcl/WindowPosSize.hxx>
#include <vcl/alpha.hxx>
#include <vcl/animate/Animation.hxx>
#include <vcl/animate/AnimationFrame.hxx>
#include <vcl/bitmap.hxx>
#include <vcl/bitmap/BitmapTypes.hxx>
#include <vcl/bitmapex.hxx>
#include <vcl/builderpage.hxx>
#include <vcl/cairo.hxx>
#include <vcl/checksum.hxx>
#include <vcl/customweld.hxx>
#include <vcl/dllapi.h>
#include <vcl/dockwin.hxx>
#include <vcl/event.hxx>
#include <vcl/fntstyle.hxx>
#include <vcl/font.hxx>
#include <vcl/gdimtf.hxx>
#include <vcl/gfxlink.hxx>
#include <vcl/gradient.hxx>
#include <vcl/graph.hxx>
#include <vcl/idle.hxx>
#include <vcl/image.hxx>
#include <vcl/kernarray.hxx>
#include <vcl/keycod.hxx>
#include <vcl/keycodes.hxx>
#include <vcl/mapmod.hxx>
#include <vcl/metaactiontypes.hxx>
#include <vcl/outdev.hxx>
#include <vcl/region.hxx>
#include <vcl/rendercontext/AddFontSubstituteFlags.hxx>
#include <vcl/rendercontext/AntialiasingFlags.hxx>
#include <vcl/rendercontext/DrawGridFlags.hxx>
#include <vcl/rendercontext/DrawImageFlags.hxx>
#include <vcl/rendercontext/DrawModeFlags.hxx>
#include <vcl/rendercontext/DrawTextFlags.hxx>
#include <vcl/rendercontext/GetDefaultFontFlags.hxx>
#include <vcl/rendercontext/ImplMapRes.hxx>
#include <vcl/rendercontext/InvertFlags.hxx>
#include <vcl/rendercontext/RasterOp.hxx>
#include <vcl/rendercontext/SalLayoutFlags.hxx>
#include <vcl/rendercontext/State.hxx>
#include <vcl/rendercontext/SystemTextColorFlags.hxx>
#include <vcl/salnativewidgets.hxx>
#include <vcl/settings.hxx>
#include <vcl/svapp.hxx>
#include <vcl/syswin.hxx>
#include <vcl/task.hxx>
#include <vcl/timer.hxx>
#include <vcl/toolboxid.hxx>
#include <vcl/uitest/factory.hxx>
#include <vcl/vclenum.hxx>
#include <vcl/vclevent.hxx>
#include <vcl/vclptr.hxx>
#include <vcl/vclreferencebase.hxx>
#include <vcl/vectorgraphicdata.hxx>
#include <vcl/wall.hxx>
#include <vcl/weld.hxx>
#include <vcl/window.hxx>
#include <vcl/windowstate.hxx>
#include <vcl/wintypes.hxx>
#endif // PCH_LEVEL >= 2
#if PCH_LEVEL >= 3
#include <basegfx/basegfxdllapi.h>
#include <basegfx/color/bcolor.hxx>
#include <basegfx/matrix/b2dhommatrix.hxx>
#include <basegfx/matrix/b3dhommatrix.hxx>
#include <basegfx/numeric/ftools.hxx>
#include <basegfx/point/b2dpoint.hxx>
#include <basegfx/point/b2ipoint.hxx>
#include <basegfx/point/b3dpoint.hxx>
#include <basegfx/polygon/b2dpolygon.hxx>
#include <basegfx/polygon/b2dpolypolygon.hxx>
#include <basegfx/polygon/b3dpolypolygon.hxx>
#include <basegfx/range/Range2D.hxx>
#include <basegfx/range/b2drange.hxx>
#include <basegfx/range/b2drectangle.hxx>
#include <basegfx/range/b2irange.hxx>
#include <basegfx/range/b3drange.hxx>
#include <basegfx/range/basicrange.hxx>
#include <basegfx/tuple/Size2D.hxx>
#include <basegfx/tuple/Tuple2D.hxx>
#include <basegfx/tuple/Tuple3D.hxx>
#include <basegfx/tuple/b2dtuple.hxx>
#include <basegfx/tuple/b2i64tuple.hxx>
#include <basegfx/tuple/b2ituple.hxx>
#include <basegfx/tuple/b3dtuple.hxx>
#include <basegfx/utils/bgradient.hxx>
#include <basegfx/utils/common.hxx>
#include <basegfx/utils/systemdependentdata.hxx>
#include <basegfx/vector/b2dsize.hxx>
#include <basegfx/vector/b2dvector.hxx>
#include <basegfx/vector/b2enums.hxx>
#include <basegfx/vector/b2isize.hxx>
#include <basegfx/vector/b2ivector.hxx>
#include <basegfx/vector/b3dvector.hxx>
#include <chartview/ChartSfxItemIds.hxx>
#include <chartview/DrawModelWrapper.hxx>
#include <com/sun/star/awt/DeviceInfo.hpp>
#include <com/sun/star/awt/FontDescriptor.hpp>
#include <com/sun/star/awt/FontSlant.hpp>
#include <com/sun/star/awt/GradientStyle.hpp>
#include <com/sun/star/awt/Key.hpp>
#include <com/sun/star/awt/KeyGroup.hpp>
#include <com/sun/star/awt/Point.hpp>
#include <com/sun/star/awt/Size.hpp>
#include <com/sun/star/beans/Pair.hpp>
#include <com/sun/star/beans/Property.hpp>
#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/beans/PropertyState.hpp>
#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/beans/XFastPropertySet.hpp>
#include <com/sun/star/beans/XMultiPropertySet.hpp>
#include <com/sun/star/beans/XMultiPropertyStates.hpp>
#include <com/sun/star/beans/XPropertiesChangeListener.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/beans/XPropertySetInfo.hpp>
#include <com/sun/star/beans/XPropertySetOption.hpp>
#include <com/sun/star/beans/XPropertyState.hpp>
#include <com/sun/star/beans/XVetoableChangeListener.hpp>
#include <com/sun/star/chart/ChartAxisPosition.hpp>
#include <com/sun/star/chart/ChartDataRowSource.hpp>
#include <com/sun/star/chart/ChartLegendExpansion.hpp>
#include <com/sun/star/chart/DataLabelPlacement.hpp>
#include <com/sun/star/chart/ErrorBarStyle.hpp>
#include <com/sun/star/chart/MissingValueTreatment.hpp>
#include <com/sun/star/chart/TimeUnit.hpp>
#include <com/sun/star/chart2/AxisType.hpp>
#include <com/sun/star/chart2/DataPointGeometry3D.hpp>
#include <com/sun/star/chart2/DataPointLabel.hpp>
#include <com/sun/star/chart2/LegendPosition.hpp>
#include <com/sun/star/chart2/RelativePosition.hpp>
#include <com/sun/star/chart2/RelativeSize.hpp>
#include <com/sun/star/chart2/StackingDirection.hpp>
#include <com/sun/star/chart2/Symbol.hpp>
#include <com/sun/star/chart2/XAxis.hpp>
#include <com/sun/star/chart2/XRegressionCurveCalculator.hpp>
#include <com/sun/star/container/XChild.hpp>
#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <com/sun/star/container/XIndexReplace.hpp>
#include <com/sun/star/container/XNameContainer.hpp>
#include <com/sun/star/container/XNamed.hpp>
#include <com/sun/star/document/XActionLockable.hpp>
#include <com/sun/star/drawing/DashStyle.hpp>
#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/drawing/HatchStyle.hpp>
#include <com/sun/star/drawing/LineCap.hpp>
#include <com/sun/star/drawing/LineJoint.hpp>
#include <com/sun/star/drawing/LineStyle.hpp>
#include <com/sun/star/drawing/PolygonKind.hpp>
#include <com/sun/star/drawing/TextFitToSizeType.hpp>
#include <com/sun/star/drawing/TextHorizontalAdjust.hpp>
#include <com/sun/star/drawing/TextVerticalAdjust.hpp>
#include <com/sun/star/drawing/XConnectorShape.hpp>
#include <com/sun/star/drawing/XControlShape.hpp>
#include <com/sun/star/drawing/XEnhancedCustomShapeDefaulter.hpp>
#include <com/sun/star/drawing/XGluePointsSupplier.hpp>
#include <com/sun/star/drawing/XShape.hpp>
#include <com/sun/star/drawing/XShapeGroup.hpp>
#include <com/sun/star/drawing/XShapes.hpp>
#include <com/sun/star/drawing/XShapes2.hpp>
#include <com/sun/star/embed/XStorage.hpp>
#include <com/sun/star/form/FormComponentType.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/frame/XModel3.hpp>
#include <com/sun/star/frame/XStatusListener.hpp>
#include <com/sun/star/frame/XToolbarController.hpp>
#include <com/sun/star/graphic/XPrimitive2D.hpp>
#include <com/sun/star/i18n/CharacterIteratorMode.hpp>
#include <com/sun/star/i18n/ForbiddenCharacters.hpp>
#include <com/sun/star/i18n/LanguageCountryInfo.hpp>
#include <com/sun/star/i18n/LocaleDataItem2.hpp>
#include <com/sun/star/i18n/LocaleItem.hpp>
#include <com/sun/star/i18n/WordType.hpp>
#include <com/sun/star/i18n/reservedWords.hpp>
#include <com/sun/star/io/XInputStream.hpp>
#include <com/sun/star/lang/DisposedException.hpp>
#include <com/sun/star/lang/EventObject.hpp>
#include <com/sun/star/lang/Locale.hpp>
#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/lang/XEventListener.hpp>
#include <com/sun/star/lang/XInitialization.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/lang/XTypeProvider.hpp>
#include <com/sun/star/lang/XUnoTunnel.hpp>
#include <com/sun/star/style/LineSpacing.hpp>
#include <com/sun/star/style/NumberingType.hpp>
#include <com/sun/star/style/TabStop.hpp>
#include <com/sun/star/style/XStyle.hpp>
#include <com/sun/star/style/XStyleSupplier.hpp>
#include <com/sun/star/text/XTextAppend.hpp>
#include <com/sun/star/text/XTextContent.hpp>
#include <com/sun/star/text/XTextCopy.hpp>
#include <com/sun/star/text/XTextCursor.hpp>
#include <com/sun/star/text/XTextRange.hpp>
#include <com/sun/star/text/XTextRangeCompare.hpp>
#include <com/sun/star/text/XTextRangeMover.hpp>
#include <com/sun/star/text/textfield/Type.hpp>
#include <com/sun/star/uno/Any.h>
#include <com/sun/star/uno/Any.hxx>
#include <com/sun/star/uno/Reference.h>
#include <com/sun/star/uno/Reference.hxx>
#include <com/sun/star/uno/RuntimeException.hpp>
#include <com/sun/star/uno/Sequence.h>
#include <com/sun/star/uno/Sequence.hxx>
#include <com/sun/star/uno/Type.h>
#include <com/sun/star/uno/Type.hxx>
#include <com/sun/star/uno/TypeClass.hdl>
#include <com/sun/star/uno/XAggregation.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <com/sun/star/uno/XInterface.hpp>
#include <com/sun/star/uno/XWeak.hpp>
#include <com/sun/star/uno/genfunc.h>
#include <com/sun/star/uno/genfunc.hxx>
#include <com/sun/star/util/Date.hpp>
#include <com/sun/star/util/DateTime.hpp>
#include <com/sun/star/util/Time.hpp>
#include <com/sun/star/util/XAccounting.hpp>
#include <com/sun/star/util/XCloneable.hpp>
#include <com/sun/star/util/XComplexColor.hpp>
#include <com/sun/star/util/XUpdatable.hpp>
#include <com/sun/star/view/XSelectionChangeListener.hpp>
#include <com/sun/star/view/XSelectionSupplier.hpp>
#include <comphelper/broadcasthelper.hxx>
#include <comphelper/compbase.hxx>
#include <comphelper/comphelperdllapi.h>
#include <comphelper/diagnose_ex.hxx>
#include <comphelper/errcode.hxx>
#include <comphelper/interfacecontainer2.hxx>
#include <comphelper/interfacecontainer4.hxx>
#include <comphelper/lok.hxx>
#include <comphelper/multicontainer2.hxx>
#include <comphelper/multiinterfacecontainer4.hxx>
#include <comphelper/processfactory.hxx>
#include <comphelper/propagg.hxx>
#include <comphelper/proparrhlp.hxx>
#include <comphelper/property.hxx>
#include <comphelper/propertycontainer.hxx>
#include <comphelper/propertycontainerhelper.hxx>
#include <comphelper/propertysetinfo.hxx>
#include <comphelper/propstate.hxx>
#include <comphelper/sequence.hxx>
#include <comphelper/servicehelper.hxx>
#include <comphelper/uno3.hxx>
#include <comphelper/unoimplbase.hxx>
#include <cppu/cppudllapi.h>
#include <cppu/unotype.hxx>
#include <cppuhelper/basemutex.hxx>
#include <cppuhelper/cppuhelperdllapi.h>
#include <cppuhelper/implbase.hxx>
#include <cppuhelper/implbase12.hxx>
#include <cppuhelper/implbase_ex.hxx>
#include <cppuhelper/implbase_ex_post.hxx>
#include <cppuhelper/implbase_ex_pre.hxx>
#include <cppuhelper/interfacecontainer.h>
#include <cppuhelper/propshlp.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <cppuhelper/weak.hxx>
#include <cppuhelper/weakagg.hxx>
#include <cppuhelper/weakref.hxx>
#include <docmodel/color/ComplexColor.hxx>
#include <docmodel/color/Transformation.hxx>
#include <docmodel/dllapi.h>
#include <docmodel/theme/ThemeColorType.hxx>
#include <drawinglayer/drawinglayerdllapi.h>
#include <drawinglayer/geometry/viewinformation2d.hxx>
#include <drawinglayer/primitive2d/CommonTypes.hxx>
#include <drawinglayer/primitive2d/Primitive2DContainer.hxx>
#include <drawinglayer/primitive2d/Primitive2DVisitor.hxx>
#include <drawinglayer/primitive2d/baseprimitive2d.hxx>
#include <editeng/EPaM.hxx>
#include <editeng/ESelection.hxx>
#include <editeng/editdata.hxx>
#include <editeng/editeng.hxx>
#include <editeng/editengdllapi.h>
#include <editeng/editobj.hxx>
#include <editeng/editstat.hxx>
#include <editeng/editview.hxx>
#include <editeng/eedata.hxx>
#include <editeng/eeitem.hxx>
#include <editeng/flditem.hxx>
#include <editeng/forbiddencharacterstable.hxx>
#include <editeng/macros.hxx>
#include <editeng/memberids.h>
#include <editeng/outliner.hxx>
#include <editeng/outlobj.hxx>
#include <editeng/overflowingtxt.hxx>
#include <editeng/paragraphdata.hxx>
#include <editeng/svxenum.hxx>
#include <editeng/svxfont.hxx>
#include <editeng/unoedsrc.hxx>
#include <editeng/unoipset.hxx>
#include <editeng/unotext.hxx>
#include <i18nlangtag/i18nlangtagdllapi.h>
#include <i18nlangtag/lang.h>
#include <i18nlangtag/languagetag.hxx>
#include <o3tl/concepts.hxx>
#include <o3tl/cow_wrapper.hxx>
#include <o3tl/deleter.hxx>
#include <o3tl/enumarray.hxx>
#include <o3tl/hash_combine.hxx>
#include <o3tl/safeint.hxx>
#include <o3tl/sorted_vector.hxx>
#include <o3tl/string_view.hxx>
#include <o3tl/strong_int.hxx>
#include <o3tl/typed_flags_set.hxx>
#include <o3tl/underlyingenumvalue.hxx>
#include <o3tl/unit_conversion.hxx>
#include <salhelper/salhelperdllapi.h>
#include <salhelper/simplereferenceobject.hxx>
#include <sfx2/basedlgs.hxx>
#include <sfx2/dllapi.h>
#include <sfx2/shell.hxx>
#include <sfx2/tabdlg.hxx>
#include <sfx2/viewsh.hxx>
#include <sot/formats.hxx>
#include <sot/sotdllapi.h>
#include <svl/SfxBroadcaster.hxx>
#include <svl/eitem.hxx>
#include <svl/hint.hxx>
#include <svl/ilstitem.hxx>
#include <svl/intitem.hxx>
#include <svl/itempool.hxx>
#include <svl/itemprop.hxx>
#include <svl/itemset.hxx>
#include <svl/languageoptions.hxx>
#include <svl/lstner.hxx>
#include <svl/numformat.hxx>
#include <svl/poolitem.hxx>
#include <svl/setitem.hxx>
#include <svl/stritem.hxx>
#include <svl/style.hxx>
#include <svl/stylesheetuser.hxx>
#include <svl/svldllapi.h>
#include <svl/typedwhich.hxx>
#include <svl/undo.hxx>
#include <svl/whichranges.hxx>
#include <svtools/colorcfg.hxx>
#include <svtools/svtdllapi.h>
#include <svx/ActionDescriptionProvider.hxx>
#include <svx/XPropertyEntry.hxx>
#include <svx/chrtitem.hxx>
#include <svx/ipolypolygoneditorcontroller.hxx>
#include <svx/itextprovider.hxx>
#include <svx/sdangitm.hxx>
#include <svx/sdr/animation/scheduler.hxx>
#include <svx/sdr/overlay/overlayobject.hxx>
#include <svx/sdr/overlay/overlayobjectlist.hxx>
#include <svx/sdr/properties/defaultproperties.hxx>
#include <svx/sdr/properties/properties.hxx>
#include <svx/sdrobjectuser.hxx>
#include <svx/sdtaditm.hxx>
#include <svx/sdtaitm.hxx>
#include <svx/sdtakitm.hxx>
#include <svx/selectioncontroller.hxx>
#include <svx/svddef.hxx>
#include <svx/svddrag.hxx>
#include <svx/svdedtv.hxx>
#include <svx/svdedxv.hxx>
#include <svx/svdgeodata.hxx>
#include <svx/svdglev.hxx>
#include <svx/svdglue.hxx>
#include <svx/svdhdl.hxx>
#include <svx/svdhlpln.hxx>
#include <svx/svdlayer.hxx>
#include <svx/svdmark.hxx>
#include <svx/svdmodel.hxx>
#include <svx/svdmrkv.hxx>
#include <svx/svdoattr.hxx>
#include <svx/svdobj.hxx>
#include <svx/svdobjkind.hxx>
#include <svx/svdoedge.hxx>
#include <svx/svdotext.hxx>
#include <svx/svdpage.hxx>
#include <svx/svdpntv.hxx>
#include <svx/svdpoev.hxx>
#include <svx/svdsnpv.hxx>
#include <svx/svdsob.hxx>
#include <svx/svdtext.hxx>
#include <svx/svdtrans.hxx>
#include <svx/svdtypes.hxx>
#include <svx/svdundo.hxx>
#include <svx/svxdllapi.h>
#include <svx/unoshape.hxx>
#include <svx/xdash.hxx>
#include <svx/xdef.hxx>
#include <svx/xhatch.hxx>
#include <svx/xit.hxx>
#include <svx/xpoly.hxx>
#include <svx/xtable.hxx>
#include <tools/UniqueID.hxx>
#include <tools/color.hxx>
#include <tools/date.hxx>
#include <tools/datetime.hxx>
#include <tools/degree.hxx>
#include <tools/fldunit.hxx>
#include <tools/fontenum.hxx>
#include <tools/fract.hxx>
#include <tools/gen.hxx>
#include <tools/helpers.hxx>
#include <tools/lineend.hxx>
#include <tools/link.hxx>
#include <tools/long.hxx>
#include <tools/mapunit.hxx>
#include <tools/poly.hxx>
#include <tools/ref.hxx>
#include <tools/solar.h>
#include <tools/stream.hxx>
#include <tools/time.hxx>
#include <tools/toolsdllapi.h>
#include <tools/weakbase.h>
#include <typelib/typeclass.h>
#include <typelib/typedescription.h>
#include <typelib/uik.h>
#include <uno/any2.h>
#include <uno/data.h>
#include <uno/sequence2.h>
#include <unotools/eventlisteneradapter.hxx>
#include <unotools/fontdefs.hxx>
#include <unotools/options.hxx>
#include <unotools/resmgr.hxx>
#include <unotools/syslocale.hxx>
#include <unotools/unotoolsdllapi.h>
#include <unotools/weakref.hxx>
#endif // PCH_LEVEL >= 3
#if PCH_LEVEL >= 4
#include <Axis.hxx>
#include <AxisHelper.hxx>
#include <AxisIndexDefines.hxx>
#include <BaseCoordinateSystem.hxx>
#include <BaseGFXHelper.hxx>
#include <CharacterProperties.hxx>
#include <CharacterPropertyItemConverter.hxx>
#include <ChartController.hxx>
#include <ChartModel.hxx>
#include <ChartType.hxx>
#include <ChartTypeHelper.hxx>
#include <ChartTypeManager.hxx>
#include <ChartTypeTemplate.hxx>
#include <ChartView.hxx>
#include <ChartWindow.hxx>
#include <Clipping.hxx>
#include <CloneHelper.hxx>
#include <CommonConverters.hxx>
#include <ControllerLockGuard.hxx>
#include <DataSeries.hxx>
#include <DataSeriesHelper.hxx>
#include <DataSeriesProperties.hxx>
#include <DataSource.hxx>
#include <DataSourceHelper.hxx>
#include <DateHelper.hxx>
#include <Diagram.hxx>
#include <DiagramHelper.hxx>
#include <DrawViewWrapper.hxx>
#include <ExplicitCategoriesProvider.hxx>
#include <FastPropertyIdRanges.hxx>
#include <FillProperties.hxx>
#include <GraphicPropertyItemConverter.hxx>
#include <GridProperties.hxx>
#include <ItemConverter.hxx>
#include <ItemPropertyMap.hxx>
#include <LabelPositionHelper.hxx>
#include <LabeledDataSequence.hxx>
#include <Legend.hxx>
#include <LegendHelper.hxx>
#include <LinePropertiesHelper.hxx>
#include <ModifyListenerHelper.hxx>
#include <NumberFormatterWrapper.hxx>
#include <OPropertySet.hxx>
#include <ObjectIdentifier.hxx>
#include <ObjectNameProvider.hxx>
#include <PlottingPositionHelper.hxx>
#include <PropertyHelper.hxx>
#include <PropertyMapper.hxx>
#include <ReferenceSizeProvider.hxx>
#include <RegressionCalculationHelper.hxx>
#include <RegressionCurveCalculator.hxx>
#include <RegressionCurveHelper.hxx>
#include <RegressionCurveModel.hxx>
#include <RelativePositionHelper.hxx>
#include <RelativeSizeHelper.hxx>
#include <ResId.hxx>
#include <ShapeFactory.hxx>
#include <StatisticsHelper.hxx>
#include <ThreeDHelper.hxx>
#include <TitleHelper.hxx>
#include <UserDefinedProperties.hxx>
#include <VLineProperties.hxx>
#include <ViewElementListProvider.hxx>
#include <WrappedProperty.hxx>
#include <defines.hxx>
#include <servicenames.hxx>
#include <servicenames_charttypes.hxx>
#include <unonames.hxx>
#endif // PCH_LEVEL >= 4

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
