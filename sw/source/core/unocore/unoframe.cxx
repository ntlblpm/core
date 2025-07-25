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

#include <com/sun/star/awt/XBitmap.hpp>
#include <com/sun/star/embed/NoVisualAreaSizeException.hpp>
#include <com/sun/star/container/XChild.hpp>
#include <com/sun/star/drawing/BitmapMode.hpp>
#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/embed/EmbedStates.hpp>
#include <com/sun/star/embed/Aspects.hpp>
#include <com/sun/star/frame/XTitle.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <o3tl/any.hxx>
#include <svx/xfillit0.hxx>
#include <svx/xflgrit.hxx>
#include <svx/sdtaitm.hxx>
#include <svx/xflclit.hxx>
#include <tools/globname.hxx>
#include <tools/UnitConversion.hxx>
#include <editeng/memberids.h>
#include <swtypes.hxx>
#include <cmdid.h>
#include <unomid.h>
#include <memory>
#include <utility>
#include <cntfrm.hxx>
#include <doc.hxx>
#include <drawdoc.hxx>
#include <IDocumentUndoRedo.hxx>
#include <IDocumentDrawModelAccess.hxx>
#include <IDocumentLayoutAccess.hxx>
#include <IDocumentStylePoolAccess.hxx>
#include <IDocumentSettingAccess.hxx>
#include <UndoAttribute.hxx>
#include <docsh.hxx>
#include <editsh.hxx>
#include <ndindex.hxx>
#include <pam.hxx>
#include <ndnotxt.hxx>
#include <svx/unomid.hxx>
#include <unocrsr.hxx>
#include <unocrsrhelper.hxx>
#include <docstyle.hxx>
#include <dcontact.hxx>
#include <fmtcnct.hxx>
#include <ndole.hxx>
#include <frmfmt.hxx>
#include <frame.hxx>
#include <textboxhelper.hxx>
#include <unotextrange.hxx>
#include <unotextcursor.hxx>
#include <unoparagraph.hxx>
#include <unomap.hxx>
#include <unoprnms.hxx>
#include <unoevent.hxx>
#include <com/sun/star/util/XModifyBroadcaster.hpp>
#include <com/sun/star/text/TextContentAnchorType.hpp>
#include <com/sun/star/text/WrapTextMode.hpp>
#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/drawing/PointSequenceSequence.hpp>
#include <com/sun/star/drawing/PointSequence.hpp>
#include <tools/poly.hxx>
#include <swundo.hxx>
#include <svx/svdpage.hxx>
#include <editeng/brushitem.hxx>
#include <editeng/protitem.hxx>
#include <fmtornt.hxx>
#include <fmteiro.hxx>
#include <fmturl.hxx>
#include <editeng/lrspitem.hxx>
#include <editeng/ulspitem.hxx>
#include <editeng/boxitem.hxx>
#include <editeng/opaqitem.hxx>
#include <editeng/prntitem.hxx>
#include <editeng/shaditem.hxx>
#include <fmtsrnd.hxx>
#include <fmtfsize.hxx>
#include <grfatr.hxx>
#include <unoframe.hxx>
#include <fmtanchr.hxx>
#include <fmtclds.hxx>
#include <fmtcntnt.hxx>
#include <frmatr.hxx>
#include <ndtxt.hxx>
#include <ndgrf.hxx>
#include <vcl/scheduler.hxx>
#include <vcl/svapp.hxx>
#include <vcl/GraphicLoader.hxx>
#include <SwStyleNameMapper.hxx>
#include <editeng/xmlcnitm.hxx>
#include <poolfmt.hxx>
#include <pagedesc.hxx>
#include <com/sun/star/style/XStyleFamiliesSupplier.hpp>
#include <editeng/frmdiritem.hxx>
#include <fmtfollowtextflow.hxx>
#include <fmtwrapinfluenceonobjpos.hxx>
#include <toolkit/helper/vclunohelper.hxx>
#include <comphelper/servicehelper.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <sal/log.hxx>
#include <vcl/errinf.hxx>
#include <unotxdoc.hxx>

#include <svx/unobrushitemhelper.hxx>
#include <svx/xbtmpit.hxx>
#include <svx/xgrscit.hxx>
#include <svx/xflbmtit.hxx>
#include <svx/xflbmpit.hxx>
#include <svx/xflbmsxy.hxx>
#include <svx/xflftrit.hxx>
#include <svx/xsflclit.hxx>
#include <svx/xflbmsli.hxx>
#include <svx/xflbtoxy.hxx>
#include <svx/xflbstit.hxx>
#include <svx/xflboxy.hxx>
#include <svx/xflbckit.hxx>
#include <svx/unoshape.hxx>
#include <svx/xflhtit.hxx>
#include <svx/xfltrit.hxx>
#include <swunohelper.hxx>
#include <fefly.hxx>
#include <formatflysplit.hxx>
#include <formatwraptextatflystart.hxx>
#include <names.hxx>

using namespace ::com::sun::star;

using ::com::sun::star::frame::XModel;
using ::com::sun::star::container::XNameAccess;
using ::com::sun::star::style::XStyleFamiliesSupplier;

class BaseFrameProperties_Impl
{
    SwUnoCursorHelper::SwAnyMapHelper m_aAnyMap;

public:
    virtual ~BaseFrameProperties_Impl();

    void            SetProperty(sal_uInt16 nWID, sal_uInt8 nMemberId, const uno::Any& rVal);
    const uno::Any* GetProperty(sal_uInt16 nWID, sal_uInt8 nMemberId);
    bool FillBaseProperties(SfxItemSet& rToSet, const SfxItemSet &rFromSet, bool& rSizeFound);

    virtual bool AnyToItemSet( SwDoc& rDoc, SfxItemSet& rFrameSet, SfxItemSet& rSet, bool& rSizeFound) = 0;
};

BaseFrameProperties_Impl::~BaseFrameProperties_Impl()
{
}

void BaseFrameProperties_Impl::SetProperty(sal_uInt16 nWID, sal_uInt8 nMemberId, const uno::Any& rVal)
{
    m_aAnyMap.SetValue( nWID, nMemberId, rVal );
}

const uno::Any* BaseFrameProperties_Impl::GetProperty(sal_uInt16 nWID, sal_uInt8 nMemberId)
{
    const uno::Any* pAny = nullptr;
    m_aAnyMap.FillValue(nWID, nMemberId, pAny);
    return pAny;
}

bool BaseFrameProperties_Impl::FillBaseProperties(SfxItemSet& rToSet, const SfxItemSet& rFromSet, bool& rSizeFound)
{
    // assert when the target SfxItemSet has no parent. It *should* have the pDfltFrameFormat
    // from SwDoc set as parent (or similar) to have the necessary XFILL_NONE in the ItemSet
    if(!rToSet.GetParent())
    {
        OSL_ENSURE(false, "OOps, target SfxItemSet *should* have a parent which contains XFILL_NONE as XFillStyleItem (!)");
    }

    bool bRet = true;
    // always add an anchor to the set
    SwFormatAnchor aAnchor ( rFromSet.Get ( RES_ANCHOR ) );
    if (const uno::Any* pAnchorType = GetProperty(RES_ANCHOR, MID_ANCHOR_ANCHORTYPE))
        bRet &= aAnchor.PutValue(*pAnchorType, MID_ANCHOR_ANCHORTYPE);
    if (const uno::Any* pAnchorPgNo = GetProperty(RES_ANCHOR, MID_ANCHOR_PAGENUM))
        bRet &= aAnchor.PutValue(*pAnchorPgNo, MID_ANCHOR_PAGENUM);

    rToSet.Put(aAnchor);

    // check for SvxBrushItem (RES_BACKGROUND) properties
    const uno::Any* pCol = GetProperty(RES_BACKGROUND, MID_BACK_COLOR);
    const uno::Any* pRGBCol = GetProperty(RES_BACKGROUND, MID_BACK_COLOR_R_G_B);
    const uno::Any* pColTrans = GetProperty(RES_BACKGROUND, MID_BACK_COLOR_TRANSPARENCY);
    const uno::Any* pTrans = GetProperty(RES_BACKGROUND, MID_GRAPHIC_TRANSPARENT);
    const uno::Any* pGrLoc = GetProperty(RES_BACKGROUND, MID_GRAPHIC_POSITION);
    const uno::Any* pGraphic = GetProperty(RES_BACKGROUND, MID_GRAPHIC);
    const uno::Any* pGrFilter = GetProperty(RES_BACKGROUND, MID_GRAPHIC_FILTER);
    const uno::Any* pGraphicURL = GetProperty(RES_BACKGROUND, MID_GRAPHIC_URL);
    const uno::Any* pGrTransparency = GetProperty(RES_BACKGROUND, MID_GRAPHIC_TRANSPARENCY);
    const bool bSvxBrushItemPropertiesUsed(
        pCol ||
        pTrans ||
        pGraphic ||
        pGraphicURL ||
        pGrFilter ||
        pGrLoc ||
        pGrTransparency ||
        pColTrans ||
        pRGBCol);

    // check for FillStyle properties in the range XATTR_FILL_FIRST, XATTR_FILL_LAST
    const uno::Any* pXFillStyleItem = GetProperty(XATTR_FILLSTYLE, 0);
    const uno::Any* pXFillColorItem = GetProperty(XATTR_FILLCOLOR, 0);

    // XFillGradientItem: two possible slots supported in UNO API
    const uno::Any* pXFillGradientItem = GetProperty(XATTR_FILLGRADIENT, MID_FILLGRADIENT);
    const uno::Any* pXFillGradientNameItem = GetProperty(XATTR_FILLGRADIENT, MID_NAME);

    // XFillHatchItem: two possible slots supported in UNO API
    const uno::Any* pXFillHatchItem = GetProperty(XATTR_FILLHATCH, MID_FILLHATCH);
    const uno::Any* pXFillHatchNameItem = GetProperty(XATTR_FILLHATCH, MID_NAME);

    // XFillBitmapItem: three possible slots supported in UNO API
    const uno::Any* pXFillBitmapItem = GetProperty(XATTR_FILLBITMAP, MID_BITMAP);
    const uno::Any* pXFillBitmapNameItem = GetProperty(XATTR_FILLBITMAP, MID_NAME);

    const uno::Any* pXFillTransparenceItem = GetProperty(XATTR_FILLTRANSPARENCE, 0);
    const uno::Any* pXGradientStepCountItem = GetProperty(XATTR_GRADIENTSTEPCOUNT, 0);
    const uno::Any* pXFillBmpPosItem = GetProperty(XATTR_FILLBMP_POS, 0);
    const uno::Any* pXFillBmpSizeXItem = GetProperty(XATTR_FILLBMP_SIZEX, 0);
    const uno::Any* pXFillBmpSizeYItem = GetProperty(XATTR_FILLBMP_SIZEY, 0);

    // XFillFloatTransparenceItem: two possible slots supported in UNO API
    const uno::Any* pXFillFloatTransparenceItem = GetProperty(XATTR_FILLFLOATTRANSPARENCE, MID_FILLGRADIENT);
    const uno::Any* pXFillFloatTransparenceNameItem = GetProperty(XATTR_FILLFLOATTRANSPARENCE, MID_NAME);

    const uno::Any* pXSecondaryFillColorItem = GetProperty(XATTR_SECONDARYFILLCOLOR, 0);
    const uno::Any* pXFillBmpSizeLogItem = GetProperty(XATTR_FILLBMP_SIZELOG, 0);
    const uno::Any* pXFillBmpTileOffsetXItem = GetProperty(XATTR_FILLBMP_TILEOFFSETX, 0);
    const uno::Any* pXFillBmpTileOffsetYItem = GetProperty(XATTR_FILLBMP_TILEOFFSETY, 0);
    const uno::Any* pXFillBmpPosOffsetXItem = GetProperty(XATTR_FILLBMP_POSOFFSETX, 0);
    const uno::Any* pXFillBmpPosOffsetYItem = GetProperty(XATTR_FILLBMP_POSOFFSETY, 0);
    const uno::Any* pXFillBackgroundItem = GetProperty(XATTR_FILLBACKGROUND, 0);
    const uno::Any* pOwnAttrFillBmpItem = GetProperty(OWN_ATTR_FILLBMP_MODE, 0);

    // tdf#91140: ignore SOLID fill style for determining if fill style is used
    // but there is a Graphic
    const bool bFillStyleUsed(pXFillStyleItem && pXFillStyleItem->hasValue() &&
        (pXFillStyleItem->get<drawing::FillStyle>() != drawing::FillStyle_SOLID || (!pGraphic || !pGraphicURL) ));
    SAL_INFO_IF(pXFillStyleItem && pXFillStyleItem->hasValue() && !bFillStyleUsed,
            "sw.uno", "FillBaseProperties: ignoring invalid FillStyle");
    const bool bXFillStyleItemUsed(
        bFillStyleUsed ||
        pXFillColorItem ||
        pXFillGradientItem || pXFillGradientNameItem ||
        pXFillHatchItem || pXFillHatchNameItem ||
        pXFillBitmapItem || pXFillBitmapNameItem ||
        pXFillTransparenceItem ||
        pXGradientStepCountItem ||
        pXFillBmpPosItem ||
        pXFillBmpSizeXItem ||
        pXFillBmpSizeYItem ||
        pXFillFloatTransparenceItem || pXFillFloatTransparenceNameItem ||
        pXSecondaryFillColorItem ||
        pXFillBmpSizeLogItem ||
        pXFillBmpTileOffsetXItem ||
        pXFillBmpTileOffsetYItem ||
        pXFillBmpPosOffsetXItem ||
        pXFillBmpPosOffsetYItem ||
        pXFillBackgroundItem ||
        pOwnAttrFillBmpItem);

    // use brush items, but *only* if no FillStyle properties are used; if both are used and when applying both
    // in the obvious order some attributes may be wrong since they are set by the 1st set, but not
    // redefined as needed by the 2nd set when they are default (and thus no tset) in the 2nd set. If
    // it is necessary for any reason to set both (it should not) an in-between step will be needed
    // that resets the items for FillAttributes in rToSet to default.
    // Note: There are other mechanisms in XMLOFF to pre-sort this relationship already, but this version
    // was used initially, is tested and works. Keep it to be able to react when another feed adds attributes
    // from both sets.
    if(bSvxBrushItemPropertiesUsed && !bXFillStyleItemUsed)
    {
        // create a temporary SvxBrushItem, fill the attributes to it and use it to set
        // the corresponding FillAttributes
        SvxBrushItem aBrush(RES_BACKGROUND);

        if(pCol)
        {
            bRet &= aBrush.PutValue(*pCol, MID_BACK_COLOR);
        }

        if(pColTrans)
        {
            bRet &= aBrush.PutValue(*pColTrans, MID_BACK_COLOR_TRANSPARENCY);
        }

        if(pRGBCol)
        {
            bRet &= aBrush.PutValue(*pRGBCol, MID_BACK_COLOR_R_G_B);
        }

        if(pTrans)
        {
            // don't overwrite transparency with a non-transparence flag
            if(!pColTrans || Any2Bool( *pTrans ))
                bRet &= aBrush.PutValue(*pTrans, MID_GRAPHIC_TRANSPARENT);
        }

        if (pGraphic)
        {
            bRet &= aBrush.PutValue(*pGraphic, MID_GRAPHIC);
        }

        if (pGraphicURL)
        {
            bRet &= aBrush.PutValue(*pGraphicURL, MID_GRAPHIC_URL);
        }

        if(pGrFilter)
        {
            bRet &= aBrush.PutValue(*pGrFilter, MID_GRAPHIC_FILTER);
        }

        if(pGrLoc)
        {
            bRet &= aBrush.PutValue(*pGrLoc, MID_GRAPHIC_POSITION);
        }

        if(pGrTransparency)
        {
            bRet &= aBrush.PutValue(*pGrTransparency, MID_GRAPHIC_TRANSPARENCY);
        }

        setSvxBrushItemAsFillAttributesToTargetSet(aBrush, rToSet);
    }

    if(bXFillStyleItemUsed)
    {
        XFillStyleItem aXFillStyleItem;
        std::unique_ptr<SvxBrushItem> aBrush(std::make_unique<SvxBrushItem>(RES_BACKGROUND));

        if(pXFillStyleItem)
        {
            aXFillStyleItem.PutValue(*pXFillStyleItem, 0);
            rToSet.Put(aXFillStyleItem);
        }

        if(pXFillColorItem)
        {
            const Color aNullCol(COL_DEFAULT_SHAPE_FILLING);
            XFillColorItem aXFillColorItem(OUString(), aNullCol);

            aXFillColorItem.PutValue(*pXFillColorItem, 0);
            rToSet.Put(aXFillColorItem);
            //set old-school brush color if we later encounter the
            //MID_BACK_COLOR_TRANSPARENCY case below
            aBrush = getSvxBrushItemFromSourceSet(rToSet, RES_BACKGROUND, false);
        }
        else if (aXFillStyleItem.GetValue() == drawing::FillStyle_SOLID && (pCol || pRGBCol))
        {
            // Fill style is set to solid, but no fill color is given.
            // On the other hand, we have a BackColor, so use that.
            if (pCol)
                aBrush->PutValue(*pCol, MID_BACK_COLOR);
            else
                aBrush->PutValue(*pRGBCol, MID_BACK_COLOR_R_G_B);
            setSvxBrushItemAsFillAttributesToTargetSet(*aBrush, rToSet);
        }

        if(pXFillGradientItem || pXFillGradientNameItem)
        {
            if(pXFillGradientItem)
            {
                // basegfx::BGradient() default already creates [COL_BLACK, COL_WHITE] as defaults
                const basegfx::BGradient aNullGrad;
                XFillGradientItem aXFillGradientItem(aNullGrad);

                aXFillGradientItem.PutValue(*pXFillGradientItem, MID_FILLGRADIENT);
                rToSet.Put(aXFillGradientItem);
            }

            if(pXFillGradientNameItem)
            {
                OUString aTempName;

                if(!(*pXFillGradientNameItem >>= aTempName ))
                {
                    throw lang::IllegalArgumentException();
                }

                bool const bSuccess = SvxShape::SetFillAttribute(
                                        XATTR_FILLGRADIENT, aTempName, rToSet);
                if (aXFillStyleItem.GetValue() == drawing::FillStyle_GRADIENT)
                {   // tdf#90946 ignore invalid gradient-name if SOLID
                    bRet &= bSuccess;
                }
                else
                {
                    SAL_INFO_IF(!bSuccess, "sw.uno",
                       "FillBaseProperties: ignoring invalid FillGradientName");
                }
            }
        }

        if(pXFillHatchItem || pXFillHatchNameItem)
        {
            if(pXFillHatchItem)
            {
                const Color aNullCol(COL_DEFAULT_SHAPE_STROKE);
                const XHatch aNullHatch(aNullCol);
                XFillHatchItem aXFillHatchItem(aNullHatch);

                aXFillHatchItem.PutValue(*pXFillHatchItem, MID_FILLHATCH);
                rToSet.Put(aXFillHatchItem);
            }

            if(pXFillHatchNameItem)
            {
                OUString aTempName;

                if(!(*pXFillHatchNameItem >>= aTempName ))
                {
                    throw lang::IllegalArgumentException();
                }

                bRet &= SvxShape::SetFillAttribute(XATTR_FILLHATCH, aTempName, rToSet);
            }
        }

        if (pXFillBitmapItem || pXFillBitmapNameItem)
        {
            if(pXFillBitmapItem)
            {
                Graphic aNullGraphic;
                XFillBitmapItem aXFillBitmapItem(std::move(aNullGraphic));

                aXFillBitmapItem.PutValue(*pXFillBitmapItem, MID_BITMAP);
                rToSet.Put(aXFillBitmapItem);
            }

            if(pXFillBitmapNameItem)
            {
                OUString aTempName;

                if(!(*pXFillBitmapNameItem >>= aTempName ))
                {
                    throw lang::IllegalArgumentException();
                }

                bRet &= SvxShape::SetFillAttribute(XATTR_FILLBITMAP, aTempName, rToSet);
            }
        }

        if (pXFillTransparenceItem)
        {
            XFillTransparenceItem aXFillTransparenceItem;
            aXFillTransparenceItem.PutValue(*pXFillTransparenceItem, 0);
            rToSet.Put(aXFillTransparenceItem);
        }
        else if (pColTrans &&
            !pXFillFloatTransparenceItem && !pXFillFloatTransparenceNameItem)
        {
            // No fill transparency is given.  On the other hand, we have a
            // BackColorTransparency, so use that.
            // tdf#90640 tdf#90130: this is necessary for LO 4.4.0 - 4.4.2
            // that forgot to write draw:opacity into documents
            // but: the value was *always* wrong for bitmaps! => ignore it
            sal_Int8 nGraphicTransparency(0);
            *pColTrans >>= nGraphicTransparency;
            if (aXFillStyleItem.GetValue() != drawing::FillStyle_BITMAP)
            {
                rToSet.Put(XFillTransparenceItem(nGraphicTransparency));
            }
            if (aXFillStyleItem.GetValue() == drawing::FillStyle_SOLID)
            {
                aBrush->PutValue(*pColTrans, MID_BACK_COLOR_TRANSPARENCY);
                setSvxBrushItemAsFillAttributesToTargetSet(*aBrush, rToSet);
            }
        }

        if(pXGradientStepCountItem)
        {
            XGradientStepCountItem aXGradientStepCountItem;

            aXGradientStepCountItem.PutValue(*pXGradientStepCountItem, 0);
            rToSet.Put(aXGradientStepCountItem);
        }

        if(pXFillBmpPosItem)
        {
            XFillBmpPosItem aXFillBmpPosItem;

            aXFillBmpPosItem.PutValue(*pXFillBmpPosItem, 0);
            rToSet.Put(aXFillBmpPosItem);
        }

        if(pXFillBmpSizeXItem)
        {
            XFillBmpSizeXItem aXFillBmpSizeXItem;

            aXFillBmpSizeXItem.PutValue(*pXFillBmpSizeXItem, 0);
            rToSet.Put(aXFillBmpSizeXItem);
        }

        if(pXFillBmpSizeYItem)
        {
            XFillBmpSizeYItem aXFillBmpSizeYItem;

            aXFillBmpSizeYItem.PutValue(*pXFillBmpSizeYItem, 0);
            rToSet.Put(aXFillBmpSizeYItem);
        }

        if(pXFillFloatTransparenceItem || pXFillFloatTransparenceNameItem)
        {
            if(pXFillFloatTransparenceItem)
            {
                // basegfx::BGradient() default already creates [COL_BLACK, COL_WHITE] as defaults
                const basegfx::BGradient aNullGrad;
                XFillFloatTransparenceItem aXFillFloatTransparenceItem(aNullGrad, false);

                aXFillFloatTransparenceItem.PutValue(*pXFillFloatTransparenceItem, MID_FILLGRADIENT);
                rToSet.Put(aXFillFloatTransparenceItem);
            }

            if(pXFillFloatTransparenceNameItem)
            {
                OUString aTempName;

                if(!(*pXFillFloatTransparenceNameItem >>= aTempName ))
                {
                    throw lang::IllegalArgumentException();
                }

                bRet &= SvxShape::SetFillAttribute(XATTR_FILLFLOATTRANSPARENCE, aTempName, rToSet);
            }
        }

        if(pXSecondaryFillColorItem)
        {
            const Color aNullCol(COL_DEFAULT_SHAPE_FILLING);
            XSecondaryFillColorItem aXSecondaryFillColorItem(OUString(), aNullCol);

            aXSecondaryFillColorItem.PutValue(*pXSecondaryFillColorItem, 0);
            rToSet.Put(aXSecondaryFillColorItem);
        }

        if(pXFillBmpSizeLogItem)
        {
            XFillBmpSizeLogItem aXFillBmpSizeLogItem;

            aXFillBmpSizeLogItem.PutValue(*pXFillBmpSizeLogItem, 0);
            rToSet.Put(aXFillBmpSizeLogItem);
        }

        if(pXFillBmpTileOffsetXItem)
        {
            XFillBmpTileOffsetXItem aXFillBmpTileOffsetXItem;

            aXFillBmpTileOffsetXItem.PutValue(*pXFillBmpTileOffsetXItem, 0);
            rToSet.Put(aXFillBmpTileOffsetXItem);
        }

        if(pXFillBmpTileOffsetYItem)
        {
            XFillBmpTileOffsetYItem aXFillBmpTileOffsetYItem;

            aXFillBmpTileOffsetYItem.PutValue(*pXFillBmpTileOffsetYItem, 0);
            rToSet.Put(aXFillBmpTileOffsetYItem);
        }

        if(pXFillBmpPosOffsetXItem)
        {
            XFillBmpPosOffsetXItem aXFillBmpPosOffsetXItem;

            aXFillBmpPosOffsetXItem.PutValue(*pXFillBmpPosOffsetXItem, 0);
            rToSet.Put(aXFillBmpPosOffsetXItem);
        }

        if(pXFillBmpPosOffsetYItem)
        {
            XFillBmpPosOffsetYItem aXFillBmpPosOffsetYItem;

            aXFillBmpPosOffsetYItem.PutValue(*pXFillBmpPosOffsetYItem, 0);
            rToSet.Put(aXFillBmpPosOffsetYItem);
        }

        if(pXFillBackgroundItem)
        {
            XFillBackgroundItem aXFillBackgroundItem;

            aXFillBackgroundItem.PutValue(*pXFillBackgroundItem, 0);
            rToSet.Put(aXFillBackgroundItem);
        }

        if(pOwnAttrFillBmpItem)
        {
            drawing::BitmapMode eMode;

            if(!(*pOwnAttrFillBmpItem >>= eMode))
            {
                sal_Int32 nMode = 0;

                if(!(*pOwnAttrFillBmpItem >>= nMode))
                {
                    throw lang::IllegalArgumentException();
                }

                eMode = static_cast<drawing::BitmapMode>(nMode);
            }

            rToSet.Put(XFillBmpStretchItem(drawing::BitmapMode_STRETCH == eMode));
            rToSet.Put(XFillBmpTileItem(drawing::BitmapMode_REPEAT == eMode));
        }
    }
    {
        const uno::Any* pCont = GetProperty(RES_PROTECT, MID_PROTECT_CONTENT);
        const uno::Any* pPos = GetProperty(RES_PROTECT, MID_PROTECT_POSITION);
        const uno::Any* pName = GetProperty(RES_PROTECT, MID_PROTECT_SIZE);
        if(pCont||pPos||pName)
        {
            SvxProtectItem aProt ( rFromSet.Get ( RES_PROTECT ) );
            if(pCont)
                bRet &= aProt.PutValue(*pCont, MID_PROTECT_CONTENT);
            if(pPos )
                bRet &= aProt.PutValue(*pPos, MID_PROTECT_POSITION);
            if(pName)
                bRet &= aProt.PutValue(*pName, MID_PROTECT_SIZE);
            rToSet.Put(aProt);
        }
    }
    {
        const uno::Any* pHori = GetProperty(RES_HORI_ORIENT, MID_HORIORIENT_ORIENT);
        const uno::Any* pHoriP = GetProperty(RES_HORI_ORIENT, MID_HORIORIENT_POSITION|CONVERT_TWIPS);
        const uno::Any* pHoriR = GetProperty(RES_HORI_ORIENT, MID_HORIORIENT_RELATION);
        const uno::Any* pPageT = GetProperty(RES_HORI_ORIENT, MID_HORIORIENT_PAGETOGGLE);
        if(pHori||pHoriP||pHoriR||pPageT)
        {
            SwFormatHoriOrient aOrient ( rFromSet.Get ( RES_HORI_ORIENT ) );
            if(pHori )
                bRet &= aOrient.PutValue(*pHori, MID_HORIORIENT_ORIENT);
            if(pHoriP)
                bRet &= aOrient.PutValue(*pHoriP, MID_HORIORIENT_POSITION | CONVERT_TWIPS);
            if(pHoriR)
                bRet &= aOrient.PutValue(*pHoriR, MID_HORIORIENT_RELATION);
            if(pPageT)
                bRet &= aOrient.PutValue(*pPageT, MID_HORIORIENT_PAGETOGGLE);
            rToSet.Put(aOrient);
        }
    }

    {
        const uno::Any* pVert = GetProperty(RES_VERT_ORIENT, MID_VERTORIENT_ORIENT);
        const uno::Any* pVertP = GetProperty(RES_VERT_ORIENT, MID_VERTORIENT_POSITION|CONVERT_TWIPS);
        const uno::Any* pVertR = GetProperty(RES_VERT_ORIENT, MID_VERTORIENT_RELATION);
        if(pVert||pVertP||pVertR)
        {
            SwFormatVertOrient aOrient ( rFromSet.Get ( RES_VERT_ORIENT ) );
            if(pVert )
                bRet &= aOrient.PutValue(*pVert, MID_VERTORIENT_ORIENT);
            if(pVertP)
                bRet &= aOrient.PutValue(*pVertP, MID_VERTORIENT_POSITION | CONVERT_TWIPS);
            if(pVertR)
                bRet &= aOrient.PutValue(*pVertR, MID_VERTORIENT_RELATION);
            rToSet.Put(aOrient);
        }
    }
    {
        const uno::Any* pURL = GetProperty(RES_URL, MID_URL_URL);
        const uno::Any* pTarget = GetProperty(RES_URL, MID_URL_TARGET);
        const uno::Any* pHyLNm = GetProperty(RES_URL, MID_URL_HYPERLINKNAME);
        const uno::Any* pHySMp = GetProperty(RES_URL, MID_URL_SERVERMAP);
        if(pURL||pTarget||pHyLNm||pHySMp)
        {
            SwFormatURL aURL ( rFromSet.Get ( RES_URL ) );
            if(pURL)
                bRet &= aURL.PutValue(*pURL, MID_URL_URL);
            if(pTarget)
                bRet &= aURL.PutValue(*pTarget, MID_URL_TARGET);
            if(pHyLNm)
                bRet &= aURL.PutValue(*pHyLNm, MID_URL_HYPERLINKNAME);
            if(pHySMp)
                bRet &= aURL.PutValue(*pHySMp, MID_URL_SERVERMAP);
            rToSet.Put(aURL);
        }
    }
    const uno::Any* pL = GetProperty(RES_LR_SPACE, MID_L_MARGIN | CONVERT_TWIPS);
    const uno::Any* pR = GetProperty(RES_LR_SPACE, MID_R_MARGIN | CONVERT_TWIPS);
    if(pL||pR)
    {
        SvxLRSpaceItem aLR ( rFromSet.Get ( RES_LR_SPACE ) );
        if(pL)
            bRet &= aLR.PutValue(*pL, MID_L_MARGIN | CONVERT_TWIPS);
        if(pR)
            bRet &= aLR.PutValue(*pR, MID_R_MARGIN | CONVERT_TWIPS);
        rToSet.Put(aLR);
    }
    const uno::Any* pT = GetProperty(RES_UL_SPACE, MID_UP_MARGIN | CONVERT_TWIPS);
    const uno::Any* pB = GetProperty(RES_UL_SPACE, MID_LO_MARGIN | CONVERT_TWIPS);
    if(pT||pB)
    {
        SvxULSpaceItem aTB ( rFromSet.Get ( RES_UL_SPACE ) );
        if(pT)
            bRet &= aTB.PutValue(*pT, MID_UP_MARGIN | CONVERT_TWIPS);
        if(pB)
            bRet &= aTB.PutValue(*pB, MID_LO_MARGIN | CONVERT_TWIPS);
        rToSet.Put(aTB);
    }
    if (const uno::Any* pOp = GetProperty(RES_OPAQUE, 0))
    {
        SvxOpaqueItem aOp ( rFromSet.Get ( RES_OPAQUE ) );
        bRet &= aOp.PutValue(*pOp, 0);
        rToSet.Put(aOp);
    }
    if (const uno::Any* pPrt = GetProperty(RES_PRINT, 0))
    {
        SvxPrintItem aPrt ( rFromSet.Get ( RES_PRINT ) );
        bRet &= aPrt.PutValue(*pPrt, 0);
        rToSet.Put(aPrt);
    }
    if (const uno::Any* pSh = GetProperty(RES_SHADOW, CONVERT_TWIPS))
    {
        SvxShadowItem aSh ( rFromSet.Get ( RES_SHADOW ) );
        bRet &= aSh.PutValue(*pSh, CONVERT_TWIPS);
        rToSet.Put(aSh);
    }
    if (const uno::Any* pShTr = GetProperty(RES_SHADOW, MID_SHADOW_TRANSPARENCE);
        pShTr && rToSet.HasItem(RES_SHADOW))
    {
        SvxShadowItem aSh(rToSet.Get(RES_SHADOW));
        bRet &= aSh.PutValue(*pShTr, MID_SHADOW_TRANSPARENCE);
        rToSet.Put(aSh);
    }
    const uno::Any* pSur = GetProperty(RES_SURROUND, MID_SURROUND_SURROUNDTYPE);
    const uno::Any* pSurAnch = GetProperty(RES_SURROUND, MID_SURROUND_ANCHORONLY);
    if(pSur || pSurAnch)
    {
        SwFormatSurround aSrnd ( rFromSet.Get ( RES_SURROUND ) );
        if(pSur)
            bRet &= aSrnd.PutValue(*pSur, MID_SURROUND_SURROUNDTYPE);
        if (const uno::Any* pSurCont = GetProperty(RES_SURROUND, MID_SURROUND_CONTOUR))
            bRet &= aSrnd.PutValue(*pSurCont, MID_SURROUND_CONTOUR);
        if(pSurAnch)
            bRet &= aSrnd.PutValue(*pSurAnch, MID_SURROUND_ANCHORONLY);
        rToSet.Put(aSrnd);
    }
    const uno::Any* pLeft = GetProperty(RES_BOX, LEFT_BORDER | CONVERT_TWIPS);
    const uno::Any* pRight = GetProperty(RES_BOX, CONVERT_TWIPS | RIGHT_BORDER);
    const uno::Any* pTop = GetProperty(RES_BOX, CONVERT_TWIPS | TOP_BORDER);
    const uno::Any* pBottom = GetProperty(RES_BOX, CONVERT_TWIPS | BOTTOM_BORDER);
    const uno::Any* pDistance = GetProperty(RES_BOX, CONVERT_TWIPS | BORDER_DISTANCE);
    const uno::Any* pLeftDistance = GetProperty(RES_BOX, CONVERT_TWIPS | LEFT_BORDER_DISTANCE);
    const uno::Any* pRightDistance = GetProperty(RES_BOX, CONVERT_TWIPS | RIGHT_BORDER_DISTANCE);
    const uno::Any* pTopDistance = GetProperty(RES_BOX, CONVERT_TWIPS | TOP_BORDER_DISTANCE);
    const uno::Any* pBottomDistance = GetProperty(RES_BOX, CONVERT_TWIPS | BOTTOM_BORDER_DISTANCE);
    const uno::Any* pLineStyle = GetProperty(RES_BOX, LINE_STYLE);
    const uno::Any* pLineWidth = GetProperty(RES_BOX, LINE_WIDTH);
    if( pLeft || pRight || pTop ||  pBottom || pDistance ||
        pLeftDistance  || pRightDistance || pTopDistance || pBottomDistance ||
        pLineStyle || pLineWidth )
    {
        SvxBoxItem aBox ( rFromSet.Get ( RES_BOX ) );
        if( pLeft )
            bRet &= aBox.PutValue(*pLeft, CONVERT_TWIPS | LEFT_BORDER);
        if( pRight )
            bRet &= aBox.PutValue(*pRight, CONVERT_TWIPS | RIGHT_BORDER);
        if( pTop )
            bRet &= aBox.PutValue(*pTop, CONVERT_TWIPS | TOP_BORDER);
        if( pBottom )
            bRet &= aBox.PutValue(*pBottom, CONVERT_TWIPS | BOTTOM_BORDER);
        if( pDistance )
            bRet &= aBox.PutValue(*pDistance, CONVERT_TWIPS | BORDER_DISTANCE);
        if( pLeftDistance )
            bRet &= aBox.PutValue(*pLeftDistance, CONVERT_TWIPS | LEFT_BORDER_DISTANCE);
        if( pRightDistance )
            bRet &= aBox.PutValue(*pRightDistance, CONVERT_TWIPS | RIGHT_BORDER_DISTANCE);
        if( pTopDistance )
            bRet &= aBox.PutValue(*pTopDistance, CONVERT_TWIPS | TOP_BORDER_DISTANCE);
        if( pBottomDistance )
            bRet &= aBox.PutValue(*pBottomDistance, CONVERT_TWIPS | BOTTOM_BORDER_DISTANCE);
        if( pLineStyle )
            bRet &= aBox.PutValue(*pLineStyle, LINE_STYLE);
        if( pLineWidth )
            bRet &= aBox.PutValue(*pLineWidth, LINE_WIDTH | CONVERT_TWIPS);
        rToSet.Put(aBox);
    }
    {
        const uno::Any* pRelH = GetProperty(RES_FRM_SIZE, MID_FRMSIZE_REL_HEIGHT);
        const uno::Any* pRelHRelation = GetProperty(RES_FRM_SIZE, MID_FRMSIZE_REL_HEIGHT_RELATION);
        const uno::Any* pRelW = GetProperty(RES_FRM_SIZE, MID_FRMSIZE_REL_WIDTH);
        const uno::Any* pRelWRelation = GetProperty(RES_FRM_SIZE, MID_FRMSIZE_REL_WIDTH_RELATION);
        const uno::Any* pSyncWidth = GetProperty(RES_FRM_SIZE, MID_FRMSIZE_IS_SYNC_WIDTH_TO_HEIGHT);
        const uno::Any* pSyncHeight = GetProperty(RES_FRM_SIZE, MID_FRMSIZE_IS_SYNC_HEIGHT_TO_WIDTH);
        const uno::Any* pWidth = GetProperty(RES_FRM_SIZE, MID_FRMSIZE_WIDTH | CONVERT_TWIPS);
        const uno::Any* pHeight = GetProperty(RES_FRM_SIZE, MID_FRMSIZE_HEIGHT | CONVERT_TWIPS);
        const uno::Any* pSize = GetProperty(RES_FRM_SIZE, MID_FRMSIZE_SIZE | CONVERT_TWIPS);
        const uno::Any* pSizeType = GetProperty(RES_FRM_SIZE, MID_FRMSIZE_SIZE_TYPE);
        const uno::Any* pWidthType = GetProperty(RES_FRM_SIZE, MID_FRMSIZE_WIDTH_TYPE);
        if( pWidth || pHeight ||pRelH || pRelHRelation || pRelW || pRelWRelation || pSize ||pSizeType ||
            pWidthType ||pSyncWidth || pSyncHeight )
        {
            rSizeFound = true;
            SwFormatFrameSize aFrameSz ( rFromSet.Get ( RES_FRM_SIZE ) );
            if(pWidth)
                bRet &= aFrameSz.PutValue(*pWidth, MID_FRMSIZE_WIDTH | CONVERT_TWIPS);
            if(pHeight)
                bRet &= aFrameSz.PutValue(*pHeight, MID_FRMSIZE_HEIGHT | CONVERT_TWIPS);
            if(pRelH )
                bRet &= aFrameSz.PutValue(*pRelH, MID_FRMSIZE_REL_HEIGHT);
            if (pRelHRelation)
                bRet &= aFrameSz.PutValue(*pRelHRelation, MID_FRMSIZE_REL_HEIGHT_RELATION);
            if(pRelW )
                bRet &= aFrameSz.PutValue(*pRelW, MID_FRMSIZE_REL_WIDTH);
            if (pRelWRelation)
                bRet &= aFrameSz.PutValue(*pRelWRelation, MID_FRMSIZE_REL_WIDTH_RELATION);
            if(pSyncWidth)
                bRet &= aFrameSz.PutValue(*pSyncWidth, MID_FRMSIZE_IS_SYNC_WIDTH_TO_HEIGHT);
            if(pSyncHeight)
                bRet &= aFrameSz.PutValue(*pSyncHeight, MID_FRMSIZE_IS_SYNC_HEIGHT_TO_WIDTH);
            if(pSize)
                bRet &= aFrameSz.PutValue(*pSize, MID_FRMSIZE_SIZE | CONVERT_TWIPS);
            if(pSizeType)
                bRet &= aFrameSz.PutValue(*pSizeType, MID_FRMSIZE_SIZE_TYPE);
            if(pWidthType)
                bRet &= aFrameSz.PutValue(*pWidthType, MID_FRMSIZE_WIDTH_TYPE);
            if(!aFrameSz.GetWidth())
                aFrameSz.SetWidth(MINFLY);
            if(!aFrameSz.GetHeight())
                aFrameSz.SetHeight(MINFLY);
            rToSet.Put(aFrameSz);
        }
        else
        {
            rSizeFound = false;
            SwFormatFrameSize aFrameSz;
            constexpr sal_Int32 constTwips_1cm = o3tl::toTwips(1, o3tl::Length::cm);
            awt::Size aSize;
            aSize.Width = constTwips_1cm;
            aSize.Height = constTwips_1cm;
            ::uno::Any aSizeVal;
            aSizeVal <<= aSize;
            aFrameSz.PutValue(aSizeVal, MID_FRMSIZE_SIZE | CONVERT_TWIPS);
            rToSet.Put(aFrameSz);
        }
    }
    if (const uno::Any* pFrameDirection = GetProperty(RES_FRAMEDIR, 0))
    {
        SvxFrameDirectionItem aAttr(SvxFrameDirection::Horizontal_LR_TB, RES_FRAMEDIR);
        aAttr.PutValue(*pFrameDirection, 0);
        rToSet.Put(aAttr);
    }
    if (const uno::Any* pUnknown = GetProperty(RES_UNKNOWNATR_CONTAINER, 0))
    {
        SvXMLAttrContainerItem aAttr(RES_UNKNOWNATR_CONTAINER);
        aAttr.PutValue(*pUnknown, 0);
        rToSet.Put(aAttr);
    }

    // #i18732#
    if (const uno::Any* pFollowTextFlow = GetProperty(RES_FOLLOW_TEXT_FLOW, MID_FOLLOW_TEXT_FLOW))
    {
        SwFormatFollowTextFlow aFormatFollowTextFlow;
        aFormatFollowTextFlow.PutValue(*pFollowTextFlow, MID_FOLLOW_TEXT_FLOW);
        rToSet.Put(aFormatFollowTextFlow);
    }

    // #i28701# - RES_WRAP_INFLUENCE_ON_OBJPOS
    const uno::Any* pWrapInfluenceOnObjPos = GetProperty(RES_WRAP_INFLUENCE_ON_OBJPOS, MID_WRAP_INFLUENCE);
    const uno::Any* pAllowOverlap = GetProperty(RES_WRAP_INFLUENCE_ON_OBJPOS, MID_ALLOW_OVERLAP);
    if ( pWrapInfluenceOnObjPos || pAllowOverlap )
    {
        SwFormatWrapInfluenceOnObjPos aFormatWrapInfluenceOnObjPos;
        if (pWrapInfluenceOnObjPos)
            aFormatWrapInfluenceOnObjPos.PutValue( *pWrapInfluenceOnObjPos, MID_WRAP_INFLUENCE );
        if (pAllowOverlap)
            aFormatWrapInfluenceOnObjPos.PutValue( *pAllowOverlap, MID_ALLOW_OVERLAP );
        rToSet.Put(aFormatWrapInfluenceOnObjPos);
    }

    if (const uno::Any* pTextVertAdjust = GetProperty(RES_TEXT_VERT_ADJUST, 0))
    {
        SdrTextVertAdjustItem aTextVertAdjust(rFromSet.Get(RES_TEXT_VERT_ADJUST));
        bRet &= aTextVertAdjust.PutValue(*pTextVertAdjust, 0);
        rToSet.Put(aTextVertAdjust);
    }

    if (const uno::Any* pDecorative = GetProperty(RES_DECORATIVE, 0))
    {
        SfxBoolItem item(RES_DECORATIVE);
        bRet &= item.PutValue(*pDecorative, 0);
        rToSet.Put(item);
    }

    if (const uno::Any* pFlySplit = GetProperty(RES_FLY_SPLIT, 0))
    {
        SwFormatFlySplit aSplit(true);
        bRet &= aSplit.PutValue(*pFlySplit, 0);
        rToSet.Put(aSplit);
    }

    if (const uno::Any* pWrapTextAtFlyStart = GetProperty(RES_WRAP_TEXT_AT_FLY_START, 0))
    {
        SwFormatWrapTextAtFlyStart aWrapTextAtFlyStart(true);
        bRet &= aWrapTextAtFlyStart.PutValue(*pWrapTextAtFlyStart, 0);
        rToSet.Put(aWrapTextAtFlyStart);
    }

    return bRet;
}

namespace {

class SwFrameProperties_Impl : public BaseFrameProperties_Impl
{
public:
    SwFrameProperties_Impl();

    bool AnyToItemSet( SwDoc& rDoc, SfxItemSet& rFrameSet, SfxItemSet& rSet, bool& rSizeFound) override;
    void FillCol(SfxItemSet& rToSet, const SfxItemSet& rFromSet);
};

}

SwFrameProperties_Impl::SwFrameProperties_Impl():
    BaseFrameProperties_Impl(/*aSwMapProvider.GetPropertyMap(PROPERTY_MAP_TEXT_FRAME)*/ )
{
}

void SwFrameProperties_Impl::FillCol(SfxItemSet& rToSet, const SfxItemSet& rFromSet)
{
    if (const uno::Any* pColumns = GetProperty(RES_COL, MID_COLUMNS))
    {
        SwFormatCol aCol ( rFromSet.Get ( RES_COL ) );
        aCol.PutValue(*pColumns, MID_COLUMNS);
        rToSet.Put(aCol);
    }
}

bool SwFrameProperties_Impl::AnyToItemSet(SwDoc& rDoc, SfxItemSet& rSet, SfxItemSet&, bool& rSizeFound)
{
    // Properties for all frames
    SwDocStyleSheet* pStyle = nullptr;
    bool bRet;

    if (const uno::Any* pStyleName = GetProperty(FN_UNO_FRAME_STYLE_NAME, 0))
    {
        OUString sStyleProgName;
        *pStyleName >>= sStyleProgName;
        UIName sStyleUIName;
        SwStyleNameMapper::FillUIName(ProgName(sStyleProgName), sStyleUIName, SwGetPoolIdFromName::FrmFmt);
        if (SwDocShell* pShell = rDoc.GetDocShell())
        {
            pStyle = static_cast<SwDocStyleSheet*>(pShell->GetStyleSheetPool()->Find(sStyleUIName.toString(),
                                                        SfxStyleFamily::Frame));
        }
    }

    if ( pStyle )
    {
        rtl::Reference< SwDocStyleSheet > xStyle( new SwDocStyleSheet( *pStyle ) );
        const ::SfxItemSet *pItemSet = &xStyle->GetItemSet();
        bRet = FillBaseProperties( rSet, *pItemSet, rSizeFound );
        FillCol(rSet, *pItemSet);
    }
    else
    {
        const ::SfxItemSet *pItemSet = &rDoc.getIDocumentStylePoolAccess().GetFrameFormatFromPool( RES_POOLFRM_FRAME )->GetAttrSet();
        bRet = FillBaseProperties( rSet, *pItemSet, rSizeFound );
        FillCol(rSet, *pItemSet);
    }
    if (const uno::Any* pEdit = GetProperty(RES_EDIT_IN_READONLY, 0))
    {
        SwFormatEditInReadonly item(RES_EDIT_IN_READONLY);
        item.PutValue(*pEdit, 0);
        rSet.Put(item);
    }
    return bRet;
}

namespace {

class SwGraphicProperties_Impl : public BaseFrameProperties_Impl
{
public:
    SwGraphicProperties_Impl();

    virtual bool AnyToItemSet( SwDoc& rDoc, SfxItemSet& rFrameSet, SfxItemSet& rSet, bool& rSizeFound) override;
    bool FillMirror(SfxItemSet& rToSet, const SfxItemSet& rFromSet);
};

}

SwGraphicProperties_Impl::SwGraphicProperties_Impl( ) :
    BaseFrameProperties_Impl(/*aSwMapProvider.GetPropertyMap(PROPERTY_MAP_TEXT_GRAPHIC)*/ )
{
}

bool SwGraphicProperties_Impl::FillMirror (SfxItemSet &rToSet, const SfxItemSet &rFromSet)
{
    const uno::Any* pHEvenMirror = GetProperty(RES_GRFATR_MIRRORGRF, MID_MIRROR_HORZ_EVEN_PAGES);
    const uno::Any* pHOddMirror = GetProperty(RES_GRFATR_MIRRORGRF, MID_MIRROR_HORZ_ODD_PAGES);
    const uno::Any* pVMirror = GetProperty(RES_GRFATR_MIRRORGRF, MID_MIRROR_VERT);
    bool bRet = true;
    if(pHEvenMirror || pHOddMirror || pVMirror )
    {
        SwMirrorGrf aMirror ( rFromSet.Get ( RES_GRFATR_MIRRORGRF ) );
        if(pHEvenMirror)
            bRet &= aMirror.PutValue(*pHEvenMirror, MID_MIRROR_HORZ_EVEN_PAGES);
        if(pHOddMirror)
            bRet &= aMirror.PutValue(*pHOddMirror, MID_MIRROR_HORZ_ODD_PAGES);
        if(pVMirror)
            bRet &= aMirror.PutValue(*pVMirror, MID_MIRROR_VERT);
        rToSet.Put(aMirror);
    }
    return bRet;
}

bool SwGraphicProperties_Impl::AnyToItemSet(
            SwDoc& rDoc,
            SfxItemSet& rFrameSet,
            SfxItemSet& rGrSet,
            bool& rSizeFound)
{
    // Properties for all frames
    bool bRet;
    SwDocStyleSheet* pStyle = nullptr;

    if (const uno::Any* pStyleName = GetProperty(FN_UNO_FRAME_STYLE_NAME, 0))
    {
        OUString sStyleProgName;
        *pStyleName >>= sStyleProgName;
        UIName sStyle;
        SwStyleNameMapper::FillUIName(ProgName(sStyleProgName), sStyle, SwGetPoolIdFromName::FrmFmt);
        if (SwDocShell* pShell = rDoc.GetDocShell())
        {
            pStyle = static_cast<SwDocStyleSheet*>(pShell->GetStyleSheetPool()->Find(sStyle.toString(),
                                                        SfxStyleFamily::Frame));
        }
    }

    if ( pStyle )
    {
        rtl::Reference< SwDocStyleSheet > xStyle( new SwDocStyleSheet(*pStyle) );
        const ::SfxItemSet *pItemSet = &xStyle->GetItemSet();
        bRet = FillBaseProperties(rFrameSet, *pItemSet, rSizeFound);
        bRet &= FillMirror(rGrSet, *pItemSet);
    }
    else
    {
        const ::SfxItemSet *pItemSet = &rDoc.getIDocumentStylePoolAccess().GetFrameFormatFromPool( RES_POOLFRM_GRAPHIC )->GetAttrSet();
        bRet = FillBaseProperties(rFrameSet, *pItemSet, rSizeFound);
        bRet &= FillMirror(rGrSet, *pItemSet);
    }

    static const ::sal_uInt16 nIDs[] =
    {
        RES_GRFATR_CROPGRF,
        RES_GRFATR_ROTATION,
        RES_GRFATR_LUMINANCE,
        RES_GRFATR_CONTRAST,
        RES_GRFATR_CHANNELR,
        RES_GRFATR_CHANNELG,
        RES_GRFATR_CHANNELB,
        RES_GRFATR_GAMMA,
        RES_GRFATR_INVERT,
        RES_GRFATR_TRANSPARENCY,
        RES_GRFATR_DRAWMODE,
    };
    for (auto nID : nIDs)
    {
        sal_uInt8 nMId = RES_GRFATR_CROPGRF == nID ? CONVERT_TWIPS : 0;
        if (const uno::Any* pAny = GetProperty(nID, nMId))
        {
            std::unique_ptr<SfxPoolItem> pItem(::GetDfltAttr(nID)->Clone());
            bRet &= pItem->PutValue(*pAny, nMId );
            rGrSet.Put(std::move(pItem));
        }
    }

    return bRet;
}

namespace {

class SwOLEProperties_Impl : public SwFrameProperties_Impl
{
public:
    SwOLEProperties_Impl() {}

    virtual bool AnyToItemSet( SwDoc& rDoc, SfxItemSet& rFrameSet, SfxItemSet& rSet, bool& rSizeFound) override;
};

}

bool SwOLEProperties_Impl::AnyToItemSet(
        SwDoc& rDoc, SfxItemSet& rFrameSet, SfxItemSet& rSet, bool& rSizeFound)
{
    if(!GetProperty(FN_UNO_CLSID, 0) && !GetProperty(FN_UNO_STREAM_NAME, 0)
         && !GetProperty(FN_EMBEDDED_OBJECT, 0)
         && !GetProperty(FN_UNO_VISIBLE_AREA_WIDTH, 0)
         && !GetProperty(FN_UNO_VISIBLE_AREA_HEIGHT, 0) )
        return false;
    SwFrameProperties_Impl::AnyToItemSet( rDoc, rFrameSet, rSet, rSizeFound);

    return true;
}

OUString SwXFrame::getImplementationName()
{
    return u"SwXFrame"_ustr;
}

sal_Bool SwXFrame::supportsService(const OUString& rServiceName)
{
    return cppu::supportsService(this, rServiceName);
}

uno::Sequence< OUString > SwXFrame::getSupportedServiceNames()
{
    return { u"com.sun.star.text.BaseFrame"_ustr, u"com.sun.star.text.TextContent"_ustr, u"com.sun.star.document.LinkTarget"_ustr };
}

SwXFrame::SwXFrame(FlyCntType eSet, const ::SfxItemPropertySet* pSet, SwDoc *pDoc)
    : m_pFrameFormat(nullptr)
    , m_pPropSet(pSet)
    , m_pDoc(pDoc)
    , m_eType(eSet)
    , m_bIsDescriptor(true)
    , m_nDrawAspect(embed::Aspects::MSOLE_CONTENT)
    , m_nVisibleAreaWidth(0)
    , m_nVisibleAreaHeight(0)
{
    SwDocShell* pShell = pDoc->GetDocShell();
    if (!pShell)
        return;

    // Register ourselves as a listener to the document (via the page descriptor)
    StartListening(pDoc->getIDocumentStylePoolAccess().GetPageDescFromPool(RES_POOLPAGE_STANDARD)->GetNotifier());
    // get the property set for the default style data
    // First get the model
    rtl::Reference < SwXTextDocument > xModel = pShell->GetBaseModel();
    // Get the style families
    uno::Reference < XNameAccess > xFamilies = xModel->getStyleFamilies();
    // Get the Frame family (and keep it for later)
    const ::uno::Any aAny = xFamilies->getByName (u"FrameStyles"_ustr);
    aAny >>= mxStyleFamily;
    // In the derived class, we'll ask mxStyleFamily for the relevant default style
    // mxStyleFamily is initialised in the SwXFrame constructor
    switch(m_eType)
    {
        case FLYCNTTYPE_FRM:
        {
            uno::Any aAny2 = mxStyleFamily->getByName (u"Frame"_ustr);
            aAny2 >>= mxStyleData;
            m_pProps.reset(new SwFrameProperties_Impl);
        }
        break;
        case FLYCNTTYPE_GRF:
        {
            uno::Any aAny2 = mxStyleFamily->getByName (u"Graphics"_ustr);
            aAny2 >>= mxStyleData;
            m_pProps.reset(new SwGraphicProperties_Impl);
        }
        break;
        case FLYCNTTYPE_OLE:
        {
            uno::Any aAny2 = mxStyleFamily->getByName (u"OLE"_ustr);
            aAny2 >>= mxStyleData;
            m_pProps.reset(new SwOLEProperties_Impl);
        }
        break;

        default:
            m_pProps.reset();
        break;
    }
}

SwXFrame::SwXFrame(SwFrameFormat& rFrameFormat, FlyCntType eSet, const ::SfxItemPropertySet* pSet)
    : m_pFrameFormat(&rFrameFormat)
    , m_pPropSet(pSet)
    , m_pDoc(nullptr)
    , m_eType(eSet)
    , m_bIsDescriptor(false)
    , m_nDrawAspect(embed::Aspects::MSOLE_CONTENT)
    , m_nVisibleAreaWidth(0)
    , m_nVisibleAreaHeight(0)
{
    StartListening(rFrameFormat.GetNotifier());
}

SwXFrame::~SwXFrame()
{
    SolarMutexGuard aGuard;
    m_pProps.reset();
    EndListeningAll();
}

template<class NameLookupIsHard>
rtl::Reference<NameLookupIsHard>
SwXFrame::CreateXFrame(SwDoc & rDoc, SwFrameFormat *const pFrameFormat)
{
    assert(!pFrameFormat || &rDoc == &pFrameFormat->GetDoc());
    rtl::Reference<NameLookupIsHard> xFrame;
    if (pFrameFormat)
    {
        xFrame = dynamic_cast<NameLookupIsHard*>(pFrameFormat->GetXObject().get().get()); // cached?
    }
    if (!xFrame.is())
    {
        if (pFrameFormat)
        {
            xFrame = new NameLookupIsHard(*pFrameFormat);
            pFrameFormat->SetXObject(cppu::getXWeak(xFrame.get()));
        }
        else
            xFrame = new NameLookupIsHard(&rDoc);
    }
    return xFrame;
}

OUString SwXFrame::getName()
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFormat = GetFrameFormat();
    if(pFormat)
        return pFormat->GetName().toString();
    if(!m_bIsDescriptor)
        throw uno::RuntimeException();
    return m_sName.toString();
}

void SwXFrame::setName(const OUString& rName)
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFormat = GetFrameFormat();
    if(pFormat)
    {
        pFormat->GetDoc().SetFlyName(static_cast<SwFlyFrameFormat&>(*pFormat), UIName(rName));
        if(pFormat->GetName() != rName)
        {
            throw uno::RuntimeException(u"SwXFrame::setName(): Illegal object name. Duplicate name?"_ustr);
        }
    }
    else if(m_bIsDescriptor)
        m_sName = UIName(rName);
    else
        throw uno::RuntimeException();
}

uno::Reference< beans::XPropertySetInfo >  SwXFrame::getPropertySetInfo()
{
    uno::Reference< beans::XPropertySetInfo >  xRef;
    static uno::Reference< beans::XPropertySetInfo >  xFrameRef;
    static uno::Reference< beans::XPropertySetInfo >  xGrfRef;
    static uno::Reference< beans::XPropertySetInfo >  xOLERef;
    switch(m_eType)
    {
    case FLYCNTTYPE_FRM:
        if( !xFrameRef.is() )
            xFrameRef = m_pPropSet->getPropertySetInfo();
        xRef = xFrameRef;
        break;
    case FLYCNTTYPE_GRF:
        if( !xGrfRef.is() )
            xGrfRef = m_pPropSet->getPropertySetInfo();
        xRef = xGrfRef;
        break;
    case FLYCNTTYPE_OLE:
        if( !xOLERef.is() )
            xOLERef = m_pPropSet->getPropertySetInfo();
        xRef = xOLERef;
        break;
    default:
        ;
    }
    return xRef;
}

SdrObject *SwXFrame::GetOrCreateSdrObject(SwFlyFrameFormat &rFormat)
{
    SdrObject* pObject = rFormat.FindSdrObject();
    if( !pObject )
    {
        SwDoc& rDoc = rFormat.GetDoc();
        // #i52858# - method name changed
        SwFlyDrawContact* pContactObject(rFormat.GetOrCreateContact());
        pObject = pContactObject->GetMaster();

        const ::SwFormatSurround& rSurround = rFormat.GetSurround();
        const IDocumentSettingAccess& rIDSA = rDoc.getIDocumentSettingAccess();
        bool isPaintHellOverHF = rIDSA.get(DocumentSettingId::PAINT_HELL_OVER_HEADER_FOOTER);
        bool bNoClippingWithWrapPolygon = rIDSA.get(DocumentSettingId::NO_CLIPPING_WITH_WRAP_POLYGON);

        if (bNoClippingWithWrapPolygon && rSurround.IsContour())
            pObject->SetLayer(rDoc.getIDocumentDrawModelAccess().GetHellId());
        else
            //TODO: HeaderFooterHellId only appropriate if object is anchored in body
            pObject->SetLayer(
            ( css::text::WrapTextMode_THROUGH == rSurround.GetSurround() &&
              !rFormat.GetOpaque().GetValue() )
                              ? isPaintHellOverHF
                                    ? rDoc.getIDocumentDrawModelAccess().GetHeaderFooterHellId()
                                    : rDoc.getIDocumentDrawModelAccess().GetHellId()
                                             : rDoc.getIDocumentDrawModelAccess().GetHeavenId() );
        SwDrawModel& rDrawModel = rDoc.getIDocumentDrawModelAccess().GetOrCreateDrawModel();
        rDrawModel.GetPage(0)->InsertObject( pObject );
    }

    return pObject;
}

static SwFrameFormat *lcl_GetFrameFormat( const ::uno::Any& rValue, SwDoc& rDoc )
{
    SwFrameFormat *pRet = nullptr;
    SwDocShell* pDocSh = rDoc.GetDocShell();
    if(pDocSh)
    {
        OUString uTemp;
        rValue >>= uTemp;
        UIName sStyle;
        SwStyleNameMapper::FillUIName(ProgName(uTemp), sStyle,
                SwGetPoolIdFromName::FrmFmt);
        SwDocStyleSheet* pStyle =
                static_cast<SwDocStyleSheet*>(pDocSh->GetStyleSheetPool()->Find(sStyle.toString(),
                                                    SfxStyleFamily::Frame));
        if(pStyle)
            pRet = pStyle->GetFrameFormat();
    }

    return pRet;
}

void SwXFrame::setPropertyValue(const OUString& rPropertyName, const ::uno::Any& _rValue)
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFormat = GetFrameFormat();
    if (!pFormat && !IsDescriptor())
        throw uno::RuntimeException();

    // Hack to support hidden property to transfer textDirection
    if(rPropertyName == "FRMDirection")
    {
        if (pFormat)
        {
            SwDocModifyAndUndoGuard guard(*pFormat);
            SvxFrameDirectionItem aItem(SvxFrameDirection::Environment, RES_FRAMEDIR);
            aItem.PutValue(_rValue, 0);
            pFormat->SetFormatAttr(aItem);
        }
        else // if(IsDescriptor())
        {
            m_pProps->SetProperty(o3tl::narrowing<sal_uInt16>(RES_FRAMEDIR), 0, _rValue);
        }
        return;
    }

    const ::SfxItemPropertyMapEntry* pEntry = m_pPropSet->getPropertyMap().getByName(rPropertyName);

    if (!pEntry)
    {
        // Hack to skip the dummy CursorNotIgnoreTables property
        if (rPropertyName != "CursorNotIgnoreTables")
            throw beans::UnknownPropertyException("Unknown property: " + rPropertyName, getXWeak());
        return;
    }

    const sal_uInt8 nMemberId(pEntry->nMemberId);
    uno::Any aValue(_rValue);

    // check for needed metric translation
    if(pEntry->nMoreFlags & PropertyMoreFlags::METRIC_ITEM)
    {
        bool bDoIt(true);

        if(XATTR_FILLBMP_SIZEX == pEntry->nWID || XATTR_FILLBMP_SIZEY == pEntry->nWID)
        {
            // exception: If these ItemTypes are used, do not convert when these are negative
            // since this means they are intended as percent values
            sal_Int32 nValue = 0;

            if(aValue >>= nValue)
            {
                bDoIt = nValue > 0;
            }
        }

        if(bDoIt)
        {
            const SwDoc* pDoc = (IsDescriptor() ? m_pDoc : &pFormat->GetDoc());
            const SfxItemPool& rPool = pDoc->GetAttrPool();
            const MapUnit eMapUnit(rPool.GetMetric(pEntry->nWID));

            if(eMapUnit != MapUnit::Map100thMM)
            {
                SvxUnoConvertFromMM(eMapUnit, aValue);
            }
        }
    }

    if(pFormat)
    {
        bool bNextFrame = false;
        if ( pEntry->nFlags & beans::PropertyAttribute::READONLY)
            throw beans::PropertyVetoException("Property is read-only: " + rPropertyName, getXWeak() );

        SwDoc& rDoc = pFormat->GetDoc();
        if ( ((m_eType == FLYCNTTYPE_GRF) && isGRFATR(pEntry->nWID)) ||
            (FN_PARAM_CONTOUR_PP         == pEntry->nWID) ||
            (FN_UNO_IS_AUTOMATIC_CONTOUR == pEntry->nWID) ||
            (FN_UNO_IS_PIXEL_CONTOUR     == pEntry->nWID) )
        {
            const ::SwNodeIndex* pIdx = pFormat->GetContent().GetContentIdx();
            if(pIdx)
            {
                SwNodeIndex aIdx(*pIdx, 1);
                SwNoTextNode* pNoText = aIdx.GetNode().GetNoTextNode();
                if(pEntry->nWID == FN_PARAM_CONTOUR_PP)
                {
                    drawing::PointSequenceSequence aParam;
                    if(!aValue.hasValue())
                        pNoText->SetContour(nullptr);
                    else if(aValue >>= aParam)
                    {
                        tools::PolyPolygon aPoly(o3tl::narrowing<sal_uInt16>(aParam.getLength()));
                        for (const ::drawing::PointSequence& rPointSeq : aParam)
                        {
                            sal_Int32 nPoints = rPointSeq.getLength();
                            const ::awt::Point* pPoints = rPointSeq.getConstArray();
                            tools::Polygon aSet( o3tl::narrowing<sal_uInt16>(nPoints) );
                            for(sal_Int32 j = 0; j < nPoints; j++)
                            {
                                Point aPoint(pPoints[j].X, pPoints[j].Y);
                                aSet.SetPoint(aPoint, o3tl::narrowing<sal_uInt16>(j));
                            }
                            // Close polygon if it isn't closed already.
                            aSet.Optimize( PolyOptimizeFlags::CLOSE );
                            aPoly.Insert( aSet );
                        }
                        pNoText->SetContourAPI( &aPoly );
                    }
                    else
                        throw lang::IllegalArgumentException();
                }
                else if(pEntry->nWID == FN_UNO_IS_AUTOMATIC_CONTOUR )
                {
                    pNoText->SetAutomaticContour( *o3tl::doAccess<bool>(aValue) );
                }
                else if(pEntry->nWID == FN_UNO_IS_PIXEL_CONTOUR )
                {
                    // The IsPixelContour property can only be set if there
                    // is no contour, or if the contour has been set by the
                    // API itself (or in other words, if the contour isn't
                    // used already).
                    if( pNoText->HasContour_() && pNoText->IsContourMapModeValid() )
                        throw lang::IllegalArgumentException();

                    pNoText->SetPixelContour( *o3tl::doAccess<bool>(aValue) );

                }
                else
                {
                    SfxItemSet aSet(pNoText->GetSwAttrSet());
                    SfxItemPropertySet::setPropertyValue(*pEntry, aValue, aSet);
                    pNoText->SetAttr(aSet);
                }
            }
        }
        // New attribute Title
        else if( FN_UNO_TITLE == pEntry->nWID )
        {
            SwFlyFrameFormat& rFlyFormat = dynamic_cast<SwFlyFrameFormat&>(*pFormat);
            OUString sTitle;
            aValue >>= sTitle;
            // assure that <SdrObject> instance exists.
            GetOrCreateSdrObject(rFlyFormat);
            rFlyFormat.GetDoc().SetFlyFrameTitle(rFlyFormat, sTitle);
        }
        else if (pEntry->nWID == FN_UNO_TOOLTIP)
        {
            SwFlyFrameFormat& rFlyFormat = dynamic_cast<SwFlyFrameFormat&>(*pFormat);
            OUString sTooltip;
            aValue >>= sTooltip;
            rFlyFormat.SetObjTooltip(sTooltip);
        }
        // New attribute Description
        else if( FN_UNO_DESCRIPTION == pEntry->nWID )
        {
            SwFlyFrameFormat& rFlyFormat = dynamic_cast<SwFlyFrameFormat&>(*pFormat);
            OUString sDescription;
            aValue >>= sDescription;
            // assure that <SdrObject> instance exists.
            GetOrCreateSdrObject(rFlyFormat);
            rFlyFormat.GetDoc().SetFlyFrameDescription(rFlyFormat, sDescription);
        }
        else if(FN_UNO_FRAME_STYLE_NAME == pEntry->nWID)
        {
            SwFrameFormat *pFrameFormat = lcl_GetFrameFormat( aValue, pFormat->GetDoc() );
            if( !pFrameFormat )
                throw lang::IllegalArgumentException();

            UnoActionContext aAction(&pFormat->GetDoc());

            std::optional<SfxItemSet> pSet;
            // #i31771#, #i25798# - No adjustment of
            // anchor ( no call of method <sw_ChkAndSetNewAnchor(..)> ),
            // if document is currently in reading mode.
            if ( !pFormat->GetDoc().IsInReading() )
            {
                // see SwFEShell::SetFrameFormat( SwFrameFormat *pNewFormat, bool bKeepOrient, Point* pDocPos )
                SwFlyFrame *pFly = nullptr;
                if (auto pFlyFrameFormat = dynamic_cast<const SwFlyFrameFormat*>(pFormat) )
                    pFly = pFlyFrameFormat->GetFrame();
                if ( pFly )
                {
                    if( const SwFormatAnchor* pItem = pFrameFormat->GetItemIfSet( RES_ANCHOR, false ))
                    {
                        pSet.emplace( rDoc.GetAttrPool(), aFrameFormatSetRange );
                        pSet->Put( *pItem );
                        if ( pFormat->GetDoc().GetEditShell() != nullptr
                             && !sw_ChkAndSetNewAnchor( *pFly, *pSet ) )
                        {
                            pSet.reset();
                        }
                    }
                }
            }

            pFormat->GetDoc().SetFrameFormatToFly( *pFormat, *pFrameFormat, pSet ? &*pSet : nullptr );
        }
        else if (FN_UNO_GRAPHIC_FILTER == pEntry->nWID)
        {
            OUString sGrfName;
            OUString sFltName;
            SwDoc::GetGrfNms( *static_cast<SwFlyFrameFormat*>(pFormat), &sGrfName, &sFltName );
            aValue >>= sFltName;
            UnoActionContext aAction(&pFormat->GetDoc());
            const ::SwNodeIndex* pIdx = pFormat->GetContent().GetContentIdx();
            if (pIdx)
            {
                SwNodeIndex aIdx(*pIdx, 1);
                SwGrfNode* pGrfNode = aIdx.GetNode().GetGrfNode();
                if(!pGrfNode)
                {
                    throw uno::RuntimeException();
                }
                SwPaM aGrfPaM(*pGrfNode);
                pFormat->GetDoc().getIDocumentContentOperations().ReRead(aGrfPaM, sGrfName, sFltName, nullptr);
            }
        }
        else if (FN_UNO_GRAPHIC == pEntry->nWID || FN_UNO_GRAPHIC_URL == pEntry->nWID)
        {
            Graphic aGraphic;
            if (aValue.has<OUString>())
            {
                OUString aURL = aValue.get<OUString>();
                if (!aURL.isEmpty())
                {
                    aGraphic = vcl::graphic::loadFromURL(aURL);
                }
            }
            else if (aValue.has<uno::Reference<graphic::XGraphic>>())
            {
                uno::Reference<graphic::XGraphic> xGraphic = aValue.get<uno::Reference<graphic::XGraphic>>();
                if (xGraphic.is())
                {
                    aGraphic = Graphic(xGraphic);
                }
            }

            if (!aGraphic.IsNone())
            {
                const ::SwNodeIndex* pIdx = pFormat->GetContent().GetContentIdx();
                if (pIdx)
                {
                    SwNodeIndex aIdx(*pIdx, 1);
                    SwGrfNode* pGrfNode = aIdx.GetNode().GetGrfNode();
                    if (!pGrfNode)
                    {
                        throw uno::RuntimeException();
                    }
                    SwPaM aGrfPaM(*pGrfNode);
                    pFormat->GetDoc().getIDocumentContentOperations().ReRead(aGrfPaM, OUString(), OUString(), &aGraphic);
                }
            }
        }
        else if (FN_UNO_REPLACEMENT_GRAPHIC == pEntry->nWID || FN_UNO_REPLACEMENT_GRAPHIC_URL == pEntry->nWID)
        {
            Graphic aGraphic;
            if (aValue.has<OUString>())
            {
                OUString aURL = aValue.get<OUString>();
                if (!aURL.isEmpty())
                {
                    aGraphic = vcl::graphic::loadFromURL(aURL);
                }
            }
            else if (aValue.has<uno::Reference<graphic::XGraphic>>())
            {
                uno::Reference<graphic::XGraphic> xGraphic = aValue.get<uno::Reference<graphic::XGraphic>>();
                if (xGraphic.is())
                {
                    aGraphic = Graphic(xGraphic);
                }
            }

            if (!aGraphic.IsNone())
            {
                const ::SwFormatContent* pCnt = &pFormat->GetContent();
                if ( pCnt->GetContentIdx() && rDoc.GetNodes()[ pCnt->GetContentIdx()->GetIndex() + 1 ] )
                {
                    SwOLENode* pOleNode =  rDoc.GetNodes()[ pCnt->GetContentIdx()->GetIndex() + 1 ]->GetOLENode();

                    if ( pOleNode )
                    {
                        svt::EmbeddedObjectRef &rEmbeddedObject = pOleNode->GetOLEObj().GetObject();
                        rEmbeddedObject.SetGraphic(aGraphic, OUString() );
                    }
                }
            }
        }
        else if((bNextFrame = (rPropertyName == UNO_NAME_CHAIN_NEXT_NAME))
            || rPropertyName == UNO_NAME_CHAIN_PREV_NAME)
        {
            OUString sChainName;
            aValue >>= sChainName;
            if (sChainName.isEmpty())
            {
                if(bNextFrame)
                    rDoc.Unchain(*pFormat);
                else
                {
                    const SwFormatChain& aChain( pFormat->GetChain() );
                    SwFrameFormat *pPrev = aChain.GetPrev();
                    if(pPrev)
                        rDoc.Unchain(*pPrev);
                }
            }
            else
            {
                SwFrameFormat* pChain = rDoc.GetFlyFrameFormatByName(UIName(sChainName));
                if(pChain)
                {
                    SwFrameFormat* pSource = bNextFrame ? pFormat : pChain;
                    SwFrameFormat* pDest = bNextFrame ? pChain: pFormat;
                    rDoc.Chain(*pSource, *pDest);
                }
            }
        }
        else if(FN_UNO_Z_ORDER == pEntry->nWID)
        {
            sal_Int32 nZOrder = - 1;
            aValue >>= nZOrder;

            // Don't set an explicit ZOrder on TextBoxes.
            if( nZOrder >= 0 && !SwTextBoxHelper::isTextBox(pFormat, RES_FLYFRMFMT) )
            {
                SdrObject* pObject =
                    GetOrCreateSdrObject( static_cast<SwFlyFrameFormat&>(*pFormat) );
                SwDrawModel *pDrawModel = rDoc.getIDocumentDrawModelAccess().GetDrawModel();
                pDrawModel->GetPage(0)->
                            SetObjectOrdNum(pObject->GetOrdNum(), nZOrder);
            }
        }
        else if(RES_ANCHOR == pEntry->nWID && MID_ANCHOR_ANCHORFRAME == nMemberId)
        {
            bool bDone = false;
            uno::Reference<text::XTextFrame> xFrame;
            if(aValue >>= xFrame)
            {
                SwXFrame* pFrame = dynamic_cast<SwXFrame*>(xFrame.get());
                if(pFrame && this != pFrame && pFrame->GetFrameFormat() && &pFrame->GetFrameFormat()->GetDoc() == &rDoc)
                {
                    SfxItemSetFixed<RES_FRMATR_BEGIN, RES_FRMATR_END - 1> aSet( rDoc.GetAttrPool() );
                    aSet.SetParent(&pFormat->GetAttrSet());
                    SwFormatAnchor aAnchor = static_cast<const SwFormatAnchor&>(aSet.Get(pEntry->nWID));

                    SwPosition aPos(*pFrame->GetFrameFormat()->GetContent().GetContentIdx());
                    aAnchor.SetAnchor(&aPos);
                    aAnchor.SetType(RndStdIds::FLY_AT_FLY);
                    aSet.Put(aAnchor);
                    rDoc.SetFlyFrameAttr( *pFormat, aSet );
                    bDone = true;
                }
            }
            if(!bDone)
                throw lang::IllegalArgumentException();
        }
        else
        {
            // standard UNO API write attributes
            // adapt former attr from SvxBrushItem::PutValue to new items XATTR_FILL_FIRST, XATTR_FILL_LAST
            SfxItemSetFixed
                <RES_FRMATR_BEGIN, RES_FRMATR_END - 1,
                RES_UNKNOWNATR_CONTAINER, RES_UNKNOWNATR_CONTAINER,

                // FillAttribute support
                XATTR_FILL_FIRST, XATTR_FILL_LAST>
                    aSet( rDoc.GetAttrPool());
            bool bDone(false);

            aSet.SetParent(&pFormat->GetAttrSet());

            if(RES_BACKGROUND == pEntry->nWID)
            {
                const SwAttrSet& rSet = pFormat->GetAttrSet();
                const std::unique_ptr<SvxBrushItem> aOriginalBrushItem(getSvxBrushItemFromSourceSet(rSet, RES_BACKGROUND, true, rDoc.IsInXMLImport()));
                std::unique_ptr<SvxBrushItem> aChangedBrushItem(aOriginalBrushItem->Clone());

                aChangedBrushItem->PutValue(aValue, nMemberId);

                if(*aChangedBrushItem != *aOriginalBrushItem)
                {
                    setSvxBrushItemAsFillAttributesToTargetSet(*aChangedBrushItem, aSet);
                    pFormat->GetDoc().SetFlyFrameAttr( *pFormat, aSet );
                }

                bDone = true;
            }
            else if(OWN_ATTR_FILLBMP_MODE == pEntry->nWID)
            {
                drawing::BitmapMode eMode;

                if(!(aValue >>= eMode))
                {
                    sal_Int32 nMode = 0;

                    if(!(aValue >>= nMode))
                    {
                        throw lang::IllegalArgumentException();
                    }

                    eMode = static_cast<drawing::BitmapMode>(nMode);
                }

                aSet.Put(XFillBmpStretchItem(drawing::BitmapMode_STRETCH == eMode));
                aSet.Put(XFillBmpTileItem(drawing::BitmapMode_REPEAT == eMode));
                pFormat->GetDoc().SetFlyFrameAttr( *pFormat, aSet );
                bDone = true;
            }

            switch(nMemberId)
            {
                case MID_NAME:
                {
                    // when named items get set, replace these with the NameOrIndex items
                    // which exist already in the pool
                    switch(pEntry->nWID)
                    {
                        case XATTR_FILLGRADIENT:
                        case XATTR_FILLHATCH:
                        case XATTR_FILLBITMAP:
                        case XATTR_FILLFLOATTRANSPARENCE:
                        {
                            OUString aTempName;

                            if(!(aValue >>= aTempName ))
                            {
                                throw lang::IllegalArgumentException();
                            }

                            bDone = SvxShape::SetFillAttribute(pEntry->nWID, aTempName, aSet);
                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }
                    break;
                }
                case MID_BITMAP:
                {
                    switch(pEntry->nWID)
                    {
                        case XATTR_FILLBITMAP:
                        {
                            Graphic aNullGraphic;
                            XFillBitmapItem aXFillBitmapItem(std::move(aNullGraphic));

                            aXFillBitmapItem.PutValue(aValue, nMemberId);
                            aSet.Put(aXFillBitmapItem);
                            bDone = true;
                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }
                    break;
                }
                default:
                {
                    break;
                }
            }

            if(!bDone)
            {
                SfxItemPropertySet::setPropertyValue(*pEntry, aValue, aSet);
            }

            if(RES_ANCHOR == pEntry->nWID && MID_ANCHOR_ANCHORTYPE == nMemberId)
            {
                SwFormatAnchor aAnchor = static_cast<const SwFormatAnchor&>(aSet.Get(pEntry->nWID));
                if(aAnchor.GetAnchorId() == RndStdIds::FLY_AT_FLY)
                {
                    const ::SwNode* pAnchorNode = aAnchor.GetAnchorNode();
                    SwFrameFormat* pFlyFormat = pAnchorNode ? pAnchorNode->GetFlyFormat() : nullptr;
                    if(!pFlyFormat || pFlyFormat->Which() == RES_DRAWFRMFMT)
                    {
                        throw lang::IllegalArgumentException(u"Anchor to frame: no frame found"_ustr, nullptr, 0);
                    }
                    else
                    {
                        SwPosition aPos = *aAnchor.GetContentAnchor();
                        aPos.Assign( *pFlyFormat->GetContent().GetContentIdx() );
                        aAnchor.SetAnchor(&aPos);
                        aSet.Put(aAnchor);
                    }
                }
                else if ((aAnchor.GetAnchorId() != RndStdIds::FLY_AT_PAGE) &&
                         !aAnchor.GetAnchorNode())
                {
                    SwNode& rNode = rDoc.GetNodes().GetEndOfContent();
                    SwPaM aPam(rNode);
                    aPam.Move( fnMoveBackward, GoInDoc );
                    aAnchor.SetAnchor( aPam.Start() );
                    aSet.Put(aAnchor);
                }

                // #i31771#, #i25798# - No adjustment of
                // anchor ( no call of method <sw_ChkAndSetNewAnchor(..)> ),
                // if document is currently in reading mode.
                if ( !pFormat->GetDoc().IsInReading() )
                {
                    // see SwFEShell::SetFlyFrameAttr( SfxItemSet& rSet )
                    SwFlyFrame *pFly = nullptr;
                    if (auto pFrameFormat = dynamic_cast<SwFlyFrameFormat*>( pFormat) )
                        pFly = pFrameFormat->GetFrame();
                    if (pFly)
                    {
                        if( const SwFormatAnchor* pItem = aSet.GetItemIfSet( RES_ANCHOR, false ))
                        {
                            aSet.Put( *pItem );
                            if ( pFormat->GetDoc().GetEditShell() != nullptr )
                            {
                                sw_ChkAndSetNewAnchor( *pFly, aSet );
                            }
                        }
                    }
                }

                pFormat->GetDoc().SetFlyFrameAttr( *pFormat, aSet );
            }
            else if(FN_UNO_CLSID == pEntry->nWID || FN_UNO_STREAM_NAME == pEntry->nWID || FN_EMBEDDED_OBJECT == pEntry->nWID)
            {
                throw lang::IllegalArgumentException();
            }
            else
            {
                SwDocModifyAndUndoGuard guard(*pFormat);
                pFormat->SetFormatAttr(aSet);
            }
        }
    }
    else // if(IsDescriptor())
    {
        m_pProps->SetProperty(pEntry->nWID, nMemberId, aValue);
        if( FN_UNO_FRAME_STYLE_NAME == pEntry->nWID )
        {
            OUString sStyleName;
            aValue >>= sStyleName;
            try
            {
                uno::Any aAny = mxStyleFamily->getByName ( sStyleName );
                aAny >>= mxStyleData;
            }
            catch ( container::NoSuchElementException const & )
            {
            }
            catch ( lang::WrappedTargetException const  & )
            {
            }
            catch ( uno::RuntimeException const & )
            {
            }
        }
        else if (FN_UNO_DRAW_ASPECT == pEntry->nWID)
        {
            OUString sAspect = u""_ustr;
            aValue >>= sAspect;

            if (sAspect == "Icon")
                m_nDrawAspect = embed::Aspects::MSOLE_ICON;
            else if (sAspect == "Content")
                m_nDrawAspect = embed::Aspects::MSOLE_CONTENT;
        }
        else if (FN_UNO_VISIBLE_AREA_WIDTH == pEntry->nWID)
        {
            OUString sAspect = u""_ustr;
            aValue >>= sAspect;
            m_nVisibleAreaWidth = sAspect.toInt64();
        }
        else if (FN_UNO_VISIBLE_AREA_HEIGHT == pEntry->nWID)
        {
            OUString sAspect = u""_ustr;
            aValue >>= sAspect;
            m_nVisibleAreaHeight = sAspect.toInt64();
        }
    }
}

namespace
{
/// Redirect error popups to developer warnings for the duration of the UNO API call.
class DisplayLockGuard
{
    bool m_bLock;

public:
    DisplayLockGuard()
    {
        m_bLock = ErrorRegistry::GetLock();
        ErrorRegistry::SetLock(true);
    }

    ~DisplayLockGuard() { ErrorRegistry::SetLock(m_bLock); }
};
}

uno::Any SwXFrame::getPropertyValue(const OUString& rPropertyName)
{
    SolarMutexGuard aGuard;
    DisplayLockGuard aDisplayGuard;
    uno::Any aAny;
    SwFrameFormat* pFormat = GetFrameFormat();
    const SfxItemPropertyMapEntry* pEntry = m_pPropSet->getPropertyMap().getByName(rPropertyName);
    if (!pEntry)
        throw beans::UnknownPropertyException( "Unknown property: " + rPropertyName, getXWeak() );

    const sal_uInt8 nMemberId(pEntry->nMemberId);

    if(FN_UNO_ANCHOR_TYPES == pEntry->nWID)
    {
        uno::Sequence<text::TextContentAnchorType> aTypes
        {
            text::TextContentAnchorType_AT_PARAGRAPH,
            text::TextContentAnchorType_AS_CHARACTER,
            text::TextContentAnchorType_AT_PAGE,
            text::TextContentAnchorType_AT_FRAME,
            text::TextContentAnchorType_AT_CHARACTER
        };
        aAny <<= aTypes;
    }
    else if(pFormat)
    {
        if( ((m_eType == FLYCNTTYPE_GRF) || (m_eType == FLYCNTTYPE_OLE)) &&
                (isGRFATR(pEntry->nWID) ||
                        pEntry->nWID == FN_PARAM_CONTOUR_PP ||
                        pEntry->nWID == FN_UNO_IS_AUTOMATIC_CONTOUR ||
                        pEntry->nWID == FN_UNO_IS_PIXEL_CONTOUR ))
        {
            const SwNodeIndex* pIdx = pFormat->GetContent().GetContentIdx();
            if(pIdx)
            {
                SwNodeIndex aIdx(*pIdx, 1);
                SwNoTextNode* pNoText = aIdx.GetNode().GetNoTextNode();
                if(pEntry->nWID == FN_PARAM_CONTOUR_PP)
                {
                    tools::PolyPolygon aContour;
                    if( pNoText->GetContourAPI( aContour ) )
                    {
                        drawing::PointSequenceSequence aPtSeq(aContour.Count());
                        drawing::PointSequence* pPSeq = aPtSeq.getArray();
                        for(sal_uInt16 i = 0; i < aContour.Count(); i++)
                        {
                            const tools::Polygon& rPoly = aContour.GetObject(i);
                            pPSeq[i].realloc(rPoly.GetSize());
                            awt::Point* pPoints = pPSeq[i].getArray();
                            for(sal_uInt16 j = 0; j < rPoly.GetSize(); j++)
                            {
                                const Point& rPoint = rPoly.GetPoint(j);
                                pPoints[j].X = rPoint.X();
                                pPoints[j].Y = rPoint.Y();
                            }
                        }
                        aAny <<= aPtSeq;
                    }
                }
                else if(pEntry->nWID == FN_UNO_IS_AUTOMATIC_CONTOUR )
                {
                    aAny <<= pNoText->HasAutomaticContour();
                }
                else if(pEntry->nWID == FN_UNO_IS_PIXEL_CONTOUR )
                {
                    aAny <<= pNoText->IsPixelContour();
                }
                else
                {
                    const SfxItemSet& aSet(pNoText->GetSwAttrSet());
                    SfxItemPropertySet::getPropertyValue(*pEntry, aSet, aAny);
                }
            }
        }
        else if (FN_UNO_REPLACEMENT_GRAPHIC == pEntry->nWID)
        {
            const SwNodeIndex* pIdx = pFormat->GetContent().GetContentIdx();
            uno::Reference<graphic::XGraphic> xGraphic;

            if (pIdx)
            {
                SwNodeIndex aIdx(*pIdx, 1);
                SwGrfNode* pGrfNode = aIdx.GetNode().GetGrfNode();
                if (!pGrfNode)
                    throw uno::RuntimeException();

                const GraphicObject* pGraphicObject = pGrfNode->GetReplacementGrfObj();

                if (pGraphicObject)
                {
                    xGraphic = pGraphicObject->GetGraphic().GetXGraphic();
                }
            }
            aAny <<= xGraphic;
        }
        else if( FN_UNO_GRAPHIC_FILTER == pEntry->nWID )
        {
            OUString sFltName;
            SwDoc::GetGrfNms( *static_cast<SwFlyFrameFormat*>(pFormat), nullptr, &sFltName );
            aAny <<= sFltName;
        }
        else if( FN_UNO_GRAPHIC_URL == pEntry->nWID )
        {
            throw uno::RuntimeException(u"Getting from this property is not supported"_ustr);
        }
        else if( FN_UNO_GRAPHIC == pEntry->nWID )
        {
            const SwNodeIndex* pIdx = pFormat->GetContent().GetContentIdx();
            if(pIdx)
            {
                SwNodeIndex aIdx(*pIdx, 1);
                SwGrfNode* pGrfNode = aIdx.GetNode().GetGrfNode();
                if(!pGrfNode)
                    throw uno::RuntimeException();
                aAny <<= pGrfNode->GetGrf().GetXGraphic();
            }
        }
        else if( FN_UNO_TRANSFORMED_GRAPHIC == pEntry->nWID
            || FN_UNO_GRAPHIC_PREVIEW == pEntry->nWID )
        {
            const SwNodeIndex* pIdx = pFormat->GetContent().GetContentIdx();
            if(pIdx)
            {
                SwNodeIndex aIdx(*pIdx, 1);
                SwGrfNode* pGrfNode = aIdx.GetNode().GetGrfNode();
                if(!pGrfNode)
                    throw uno::RuntimeException();

                SwDoc& rDoc = pFormat->GetDoc();
                if (const SwEditShell* pEditShell = rDoc.GetEditShell())
                {
                    SwFrame* pCurrFrame = pEditShell->GetCurrFrame(false);
                    GraphicAttr aGraphicAttr;
                    pGrfNode->GetGraphicAttr( aGraphicAttr, pCurrFrame );
                    const GraphicObject aGraphicObj = pGrfNode->GetGrfObj();

                    awt::Size aFrameSize = getSize();
                    Size aSize100thmm(aFrameSize.Width, aFrameSize.Height);
                    Size aSize = OutputDevice::LogicToLogic(aSize100thmm, MapMode(MapUnit::Map100thMM), aGraphicObj.GetPrefMapMode());

                    if (FN_UNO_GRAPHIC_PREVIEW == pEntry->nWID)
                    {
                        double fX = static_cast<double>(aSize.getWidth()) / 1280;
                        double fY = static_cast<double>(aSize.getHeight()) / 720;
                        double fFactor = fX > fY ? fX : fY;
                        if (fFactor > 1.0)
                        {
                            aSize.setWidth(aSize.getWidth() / fFactor);
                            aSize.setHeight(aSize.getHeight() / fFactor);
                        }
                    }

                    Graphic aGraphic = aGraphicObj.GetTransformedGraphic(aSize, aGraphicObj.GetPrefMapMode(), aGraphicAttr);
                    aAny <<= aGraphic.GetXGraphic();
                }
            }
        }
        else if(FN_UNO_FRAME_STYLE_NAME == pEntry->nWID)
        {
            aAny <<= SwStyleNameMapper::GetProgName(pFormat->DerivedFrom()->GetName(), SwGetPoolIdFromName::FrmFmt ).toString();
        }
        // #i73249#
        else if( FN_UNO_TITLE == pEntry->nWID )
        {
            SwFlyFrameFormat& rFlyFormat = dynamic_cast<SwFlyFrameFormat&>(*pFormat);
            // assure that <SdrObject> instance exists.
            GetOrCreateSdrObject(rFlyFormat);
            aAny <<= rFlyFormat.GetObjTitle();
        }
        else if (pEntry->nWID == FN_UNO_TOOLTIP)
        {
            SwFlyFrameFormat& rFlyFormat = dynamic_cast<SwFlyFrameFormat&>(*pFormat);
            aAny <<= rFlyFormat.GetObjTooltip();
        }
        // New attribute Description
        else if( FN_UNO_DESCRIPTION == pEntry->nWID )
        {
            SwFlyFrameFormat& rFlyFormat = dynamic_cast<SwFlyFrameFormat&>(*pFormat);
            // assure that <SdrObject> instance exists.
            GetOrCreateSdrObject(rFlyFormat);
            aAny <<= rFlyFormat.GetObjDescription();
        }
        else if(m_eType == FLYCNTTYPE_GRF &&
                (rPropertyName == UNO_NAME_ACTUAL_SIZE))
        {
            const SwNodeIndex* pIdx = pFormat->GetContent().GetContentIdx();
            if(pIdx)
            {
                SwNodeIndex aIdx(*pIdx, 1);
                Size aActSize = aIdx.GetNode().GetNoTextNode()->GetTwipSize();
                awt::Size aTmp;
                aTmp.Width = convertTwipToMm100(aActSize.Width());
                aTmp.Height = convertTwipToMm100(aActSize.Height());
                aAny <<= aTmp;
            }
        }
        else if(FN_PARAM_LINK_DISPLAY_NAME == pEntry->nWID)
        {
            aAny <<= pFormat->GetName().toString();
        }
        else if(FN_UNO_Z_ORDER == pEntry->nWID)
        {
            const SdrObject* pObj = pFormat->FindRealSdrObject();
            if( pObj == nullptr )
                pObj = pFormat->FindSdrObject();
            if( pObj )
            {
                aAny <<= static_cast<sal_Int32>(pObj->GetOrdNum());
            }
        }
        else if(FN_UNO_CLSID == pEntry->nWID || FN_UNO_MODEL == pEntry->nWID||
                FN_UNO_COMPONENT == pEntry->nWID ||FN_UNO_STREAM_NAME == pEntry->nWID||
                FN_EMBEDDED_OBJECT == pEntry->nWID)
        {
            SwDoc& rDoc = pFormat->GetDoc();
            const SwFormatContent* pCnt = &pFormat->GetContent();
            OSL_ENSURE( pCnt->GetContentIdx() &&
                           rDoc.GetNodes()[ pCnt->GetContentIdx()->
                                            GetIndex() + 1 ]->GetOLENode(), "no OLE-Node?");

            SwOLENode* pOleNode =  rDoc.GetNodes()[ pCnt->GetContentIdx()
                                            ->GetIndex() + 1 ]->GetOLENode();
            uno::Reference < embed::XEmbeddedObject > xIP = pOleNode->GetOLEObj().GetOleRef();
            OUString aHexCLSID;
            {
                SvGlobalName aClassName( xIP->getClassID() );
                aHexCLSID = aClassName.GetHexName();
                if(FN_UNO_CLSID != pEntry->nWID)
                {
                    if ( svt::EmbeddedObjectRef::TryRunningState( xIP ) )
                    {
                        uno::Reference < lang::XComponent > xComp( xIP->getComponent(), uno::UNO_QUERY );
                        uno::Reference < frame::XModel > xModel( xComp, uno::UNO_QUERY );
                        if ( FN_EMBEDDED_OBJECT == pEntry->nWID )
                        {
                            // when exposing the EmbeddedObject, ensure it has a client site
                            SwDocShell* pShell = rDoc.GetDocShell();
                            OSL_ENSURE( pShell, "no doc shell => no client site" );
                            if ( pShell )
                                pShell->GetIPClient( svt::EmbeddedObjectRef( xIP, embed::Aspects::MSOLE_CONTENT ) );
                            aAny <<= xIP;
                        }
                        else if ( xModel.is() )
                            aAny <<= xModel;
                        else if ( FN_UNO_COMPONENT == pEntry->nWID )
                            aAny <<= xComp;
                    }
                }
            }

            if(FN_UNO_CLSID == pEntry->nWID)
                aAny <<= aHexCLSID;
            else if(FN_UNO_STREAM_NAME == pEntry->nWID)
            {
                aAny <<= pOleNode->GetOLEObj().GetCurrentPersistName();
            }
            else if(FN_EMBEDDED_OBJECT == pEntry->nWID)
            {
                aAny <<= pOleNode->GetOLEObj().GetOleRef();
            }
        }
        else if(WID_LAYOUT_SIZE == pEntry->nWID)
        {
            // format document completely in order to get correct value (no EditShell for ole embedded case)
            if (SwEditShell* pEditShell = pFormat->GetDoc().GetEditShell())
                pEditShell->CalcLayout();

            SwFrame* pTmpFrame = SwIterator<SwFrame,SwFormat>( *pFormat ).First();
            if ( pTmpFrame )
            {
                OSL_ENSURE( pTmpFrame->isFrameAreaDefinitionValid(), "frame not valid" );
                const SwRect &rRect = pTmpFrame->getFrameArea();
                Size aMM100Size = o3tl::convert(
                        Size( rRect.Width(), rRect.Height() ),
                        o3tl::Length::twip, o3tl::Length::mm100 );
                aAny <<= awt::Size( aMM100Size.Width(), aMM100Size.Height() );
            }
        }
        else if(pEntry->nWID == FN_UNO_PARENT_TEXT)
        {
            if (!m_xParentText.is())
            {
                const SwFormatAnchor& rFormatAnchor = pFormat->GetAnchor();
                if (rFormatAnchor.GetAnchorNode())
                {
                    m_xParentText = sw::CreateParentXText(pFormat->GetDoc(), *rFormatAnchor.GetContentAnchor());
                }
            }
            aAny <<= m_xParentText;
        }
        else
        {
            // standard UNO API read attributes
            // adapt former attr from SvxBrushItem::PutValue to new items XATTR_FILL_FIRST, XATTR_FILL_LAST
            const SwAttrSet& rSet = pFormat->GetAttrSet();
            bool bDone(false);

            if(RES_BACKGROUND == pEntry->nWID)
            {
                const std::unique_ptr<SvxBrushItem> aOriginalBrushItem(getSvxBrushItemFromSourceSet(rSet, RES_BACKGROUND));

                if(!aOriginalBrushItem->QueryValue(aAny, nMemberId))
                {
                    OSL_ENSURE(false, "Error getting attribute from RES_BACKGROUND (!)");
                }

                bDone = true;
            }
            else if(OWN_ATTR_FILLBMP_MODE == pEntry->nWID)
            {
                if (rSet.Get(XATTR_FILLBMP_TILE).GetValue())
                {
                    aAny <<= drawing::BitmapMode_REPEAT;
                }
                else if (rSet.Get(XATTR_FILLBMP_STRETCH).GetValue())
                {
                    aAny <<= drawing::BitmapMode_STRETCH;
                }
                else
                {
                    aAny <<= drawing::BitmapMode_NO_REPEAT;
                }

                bDone = true;
            }

            if(!bDone)
            {
                SfxItemPropertySet::getPropertyValue(*pEntry, rSet, aAny);
            }
        }
    }
    else if(IsDescriptor())
    {
        if ( ! m_pDoc )
            throw uno::RuntimeException();
        if(WID_LAYOUT_SIZE != pEntry->nWID)  // there is no LayoutSize in a descriptor
        {
            if (const uno::Any* pAny = m_pProps->GetProperty(pEntry->nWID, nMemberId))
                aAny = *pAny;
            else
                aAny = mxStyleData->getPropertyValue( rPropertyName );
        }
    }
    else
        throw uno::RuntimeException();

    if (pEntry->aType == ::cppu::UnoType<sal_Int16>::get() && pEntry->aType != aAny.getValueType())
    {
        // since the sfx uint16 item now exports a sal_Int32, we may have to fix this here
        sal_Int32 nValue = 0;
        aAny >>= nValue;
        aAny <<= static_cast<sal_Int16>(nValue);
    }

    // check for needed metric translation
    if(pEntry->nMoreFlags & PropertyMoreFlags::METRIC_ITEM)
    {
        bool bDoIt(true);

        if(XATTR_FILLBMP_SIZEX == pEntry->nWID || XATTR_FILLBMP_SIZEY == pEntry->nWID)
        {
            // exception: If these ItemTypes are used, do not convert when these are negative
            // since this means they are intended as percent values
            sal_Int32 nValue = 0;

            if(aAny >>= nValue)
            {
                bDoIt = nValue > 0;
            }
        }

        if(bDoIt)
        {
            const SwDoc* pDoc = (IsDescriptor() ? m_pDoc : &GetFrameFormat()->GetDoc());
            const SfxItemPool& rPool = pDoc->GetAttrPool();
            const MapUnit eMapUnit(rPool.GetMetric(pEntry->nWID));

            if(eMapUnit != MapUnit::Map100thMM)
            {
                SvxUnoConvertToMM(eMapUnit, aAny);
            }
        }
    }

    return aAny;
}

void SwXFrame::addPropertyChangeListener(const OUString& /*PropertyName*/,
    const uno::Reference< beans::XPropertyChangeListener > & /*aListener*/)
{
    OSL_FAIL("not implemented");
}

void SwXFrame::removePropertyChangeListener(const OUString& /*PropertyName*/,
    const uno::Reference< beans::XPropertyChangeListener > & /*aListener*/)
{
    OSL_FAIL("not implemented");
}

void SwXFrame::addVetoableChangeListener(const OUString& /*PropertyName*/,
                                const uno::Reference< beans::XVetoableChangeListener > & /*aListener*/)
{
    OSL_FAIL("not implemented");
}

void SwXFrame::removeVetoableChangeListener(
    const OUString& /*PropertyName*/, const uno::Reference< beans::XVetoableChangeListener > & /*aListener*/)
{
    OSL_FAIL("not implemented");
}

beans::PropertyState SwXFrame::getPropertyState( const OUString& rPropertyName )
{
    SolarMutexGuard aGuard;
    uno::Sequence< OUString > aPropertyNames { rPropertyName };
    uno::Sequence< beans::PropertyState > aStates = getPropertyStates(aPropertyNames);
    return aStates.getConstArray()[0];
}

uno::Sequence< beans::PropertyState > SwXFrame::getPropertyStates(
    const uno::Sequence< OUString >& aPropertyNames )
{
    SolarMutexGuard aGuard;
    uno::Sequence< beans::PropertyState > aStates(aPropertyNames.getLength());
    auto [pStates, end] = asNonConstRange(aStates);
    SwFrameFormat* pFormat = GetFrameFormat();
    if(pFormat)
    {
        const OUString* pNames = aPropertyNames.getConstArray();
        const SwAttrSet& rFormatSet = pFormat->GetAttrSet();
        for(int i = 0; i < aPropertyNames.getLength(); i++)
        {
            const SfxItemPropertyMapEntry* pEntry = m_pPropSet->getPropertyMap().getByName(pNames[i]);
            if (!pEntry)
                throw beans::UnknownPropertyException("Unknown property: " + pNames[i], getXWeak() );

            if(pEntry->nWID == FN_UNO_ANCHOR_TYPES||
                pEntry->nWID == FN_PARAM_LINK_DISPLAY_NAME||
                FN_UNO_FRAME_STYLE_NAME == pEntry->nWID||
                FN_UNO_GRAPHIC == pEntry->nWID||
                FN_UNO_GRAPHIC_URL == pEntry->nWID||
                FN_UNO_GRAPHIC_FILTER == pEntry->nWID||
                FN_UNO_ACTUAL_SIZE == pEntry->nWID||
                FN_UNO_ALTERNATIVE_TEXT == pEntry->nWID)
            {
                pStates[i] = beans::PropertyState_DIRECT_VALUE;
            }
            else if(OWN_ATTR_FILLBMP_MODE == pEntry->nWID)
            {
                if(SfxItemState::SET == rFormatSet.GetItemState(XATTR_FILLBMP_STRETCH, false)
                    || SfxItemState::SET == rFormatSet.GetItemState(XATTR_FILLBMP_TILE, false))
                {
                    pStates[i] = beans::PropertyState_DIRECT_VALUE;
                }
                else
                {
                    pStates[i] = beans::PropertyState_AMBIGUOUS_VALUE;
                }
            }
            // for FlyFrames we need to mark the used properties from type RES_BACKGROUND
            // as beans::PropertyState_DIRECT_VALUE to let users of this property call
            // getPropertyValue where the member properties will be mapped from the
            // fill attributes to the according SvxBrushItem entries
            else if (RES_BACKGROUND == pEntry->nWID)
            {
                if (SWUnoHelper::needToMapFillItemsToSvxBrushItemTypes(rFormatSet, pEntry->nMemberId))
                    pStates[i] = beans::PropertyState_DIRECT_VALUE;
                else
                    pStates[i] = beans::PropertyState_DEFAULT_VALUE;
            }
            else
            {
                if ((m_eType == FLYCNTTYPE_GRF) && isGRFATR(pEntry->nWID))
                {
                    const SwNodeIndex* pIdx = pFormat->GetContent().GetContentIdx();
                    if(pIdx)
                    {
                        SwNodeIndex aIdx(*pIdx, 1);
                        SwNoTextNode* pNoText = aIdx.GetNode().GetNoTextNode();
                        const SfxItemSet& aSet(pNoText->GetSwAttrSet());
                        aSet.GetItemState(pEntry->nWID);
                        if(SfxItemState::SET == aSet.GetItemState( pEntry->nWID, false ))
                            pStates[i] = beans::PropertyState_DIRECT_VALUE;
                    }
                }
                else
                {
                    if(SfxItemState::SET == rFormatSet.GetItemState( pEntry->nWID, false ))
                        pStates[i] = beans::PropertyState_DIRECT_VALUE;
                    else
                        pStates[i] = beans::PropertyState_DEFAULT_VALUE;
                }
            }
        }
    }
    else if(IsDescriptor())
    {
        std::fill(pStates, end, beans::PropertyState_DIRECT_VALUE);
    }
    else
        throw uno::RuntimeException();
    return aStates;
}

void SwXFrame::setPropertyToDefault( const OUString& rPropertyName )
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFormat = GetFrameFormat();
    if(pFormat)
    {
        const SfxItemPropertyMapEntry* pEntry = m_pPropSet->getPropertyMap().getByName(rPropertyName);
        if (!pEntry)
            throw beans::UnknownPropertyException( "Unknown property: " + rPropertyName, getXWeak() );
        if ( pEntry->nFlags & beans::PropertyAttribute::READONLY)
            throw uno::RuntimeException("setPropertyToDefault: property is read-only: " + rPropertyName, getXWeak() );

        if(OWN_ATTR_FILLBMP_MODE == pEntry->nWID)
        {
            SwDoc& rDoc = pFormat->GetDoc();
            SfxItemSetFixed<XATTR_FILL_FIRST, XATTR_FILL_LAST> aSet(rDoc.GetAttrPool());
            aSet.SetParent(&pFormat->GetAttrSet());

            aSet.ClearItem(XATTR_FILLBMP_STRETCH);
            aSet.ClearItem(XATTR_FILLBMP_TILE);

            SwDocModifyAndUndoGuard guard(*pFormat);
            pFormat->SetFormatAttr(aSet);
        }
        else if( pEntry->nWID &&
            pEntry->nWID != FN_UNO_ANCHOR_TYPES &&
            pEntry->nWID != FN_PARAM_LINK_DISPLAY_NAME)
        {
            if ( (m_eType == FLYCNTTYPE_GRF) && isGRFATR(pEntry->nWID) )
            {
                const SwNodeIndex* pIdx = pFormat->GetContent().GetContentIdx();
                if(pIdx)
                {
                    SwNodeIndex aIdx(*pIdx, 1);
                    SwNoTextNode* pNoText = aIdx.GetNode().GetNoTextNode();
                    {
                        SfxItemSet aSet(pNoText->GetSwAttrSet());
                        aSet.ClearItem(pEntry->nWID);
                        pNoText->SetAttr(aSet);
                    }
                }
            }
            // #i73249#
            else if( FN_UNO_TITLE == pEntry->nWID )
            {
                SwFlyFrameFormat& rFlyFormat = dynamic_cast<SwFlyFrameFormat&>(*pFormat);
                // assure that <SdrObject> instance exists.
                GetOrCreateSdrObject(rFlyFormat);
                rFlyFormat.GetDoc().SetFlyFrameTitle(rFlyFormat, OUString());
            }
            // New attribute Description
            else if( FN_UNO_DESCRIPTION == pEntry->nWID )
            {
                SwFlyFrameFormat& rFlyFormat = dynamic_cast<SwFlyFrameFormat&>(*pFormat);
                // assure that <SdrObject> instance exists.
                GetOrCreateSdrObject(rFlyFormat);
                rFlyFormat.GetDoc().SetFlyFrameDescription(rFlyFormat, OUString());
            }
            else if (rPropertyName != UNO_NAME_ANCHOR_TYPE)
            {
                SwDoc& rDoc = pFormat->GetDoc();
                SfxItemSetFixed<RES_FRMATR_BEGIN, RES_FRMATR_END - 1> aSet( rDoc.GetAttrPool() );
                aSet.SetParent(&pFormat->GetAttrSet());
                aSet.ClearItem(pEntry->nWID);
                SwDocModifyAndUndoGuard guard(*pFormat);
                pFormat->SetFormatAttr(aSet);
            }
        }
        else
        {
            bool bNextFrame = rPropertyName == UNO_NAME_CHAIN_NEXT_NAME;
            if( bNextFrame || rPropertyName == UNO_NAME_CHAIN_PREV_NAME )
            {
                SwDoc& rDoc = pFormat->GetDoc();
                if(bNextFrame)
                    rDoc.Unchain(*pFormat);
                else
                {
                    const SwFormatChain& aChain( pFormat->GetChain() );
                    SwFrameFormat *pPrev = aChain.GetPrev();
                    if(pPrev)
                        rDoc.Unchain(*pPrev);
                }
            }
        }
    }
    else if(!IsDescriptor())
        throw uno::RuntimeException();

}

uno::Any SwXFrame::getPropertyDefault( const OUString& rPropertyName )
{
    SolarMutexGuard aGuard;
    uno::Any aRet;
    SwFrameFormat* pFormat = GetFrameFormat();
    if(pFormat)
    {
        const SfxItemPropertyMapEntry* pEntry = m_pPropSet->getPropertyMap().getByName(rPropertyName);
        if(!pEntry)
            throw beans::UnknownPropertyException( "Unknown property: " + rPropertyName, getXWeak() );

        if ( pEntry->nWID < RES_FRMATR_END )
        {
            const SfxPoolItem& rDefItem =
                pFormat->GetDoc().GetAttrPool().GetUserOrPoolDefaultItem(pEntry->nWID);
            rDefItem.QueryValue(aRet, pEntry->nMemberId);
        }

    }
    else if(!IsDescriptor())
        throw uno::RuntimeException();
    return aRet;
}

void SAL_CALL SwXFrame::addEventListener(
        const uno::Reference<lang::XEventListener> & xListener)
{
    std::unique_lock aGuard(m_Mutex);
    m_EventListeners.addInterface(aGuard, xListener);
}

void SAL_CALL SwXFrame::removeEventListener(
        const uno::Reference<lang::XEventListener> & xListener)
{
    std::unique_lock aGuard(m_Mutex);
    m_EventListeners.removeInterface(aGuard, xListener);
}

void SwXFrame::DisposeInternal()
{
    mxStyleData.clear();
    mxStyleFamily.clear();
    m_pDoc = nullptr;
    if (m_refCount == 0)
    {   // fdo#72695: if UNO object is already dead, don't revive it with event
        return;
    }
    {
        lang::EventObject const ev(static_cast<cppu::OWeakObject*>(this));
        std::unique_lock aGuard(m_Mutex);
        m_EventListeners.disposeAndClear(aGuard, ev);
    }
    m_pFrameFormat = nullptr;
    EndListeningAll();
}
void SwXFrame::Notify(const SfxHint& rHint)
{
    if(rHint.GetId() == SfxHintId::Dying)
        DisposeInternal();
}

void SwXFrame::dispose()
{
    SolarMutexGuard aGuard;
    Scheduler::IdlesLockGuard g;
    SwFrameFormat* pFormat = GetFrameFormat();
    if (!pFormat)
        return;

    DisposeInternal();
    SdrObject* pObj = pFormat->FindSdrObject();
    // OD 11.09.2003 #112039# - add condition to perform delete of
    // format/anchor sign, not only if the object is inserted, but also
    // if a contact object is registered, which isn't in the destruction.
    if ( pObj &&
         ( pObj->IsInserted() ||
           ( pObj->GetUserCall() &&
             !static_cast<SwContact*>(pObj->GetUserCall())->IsInDTOR() ) ) )
    {
        const SwFormatAnchor& rFormatAnchor = pFormat->GetAnchor();
        if (rFormatAnchor.GetAnchorId() == RndStdIds::FLY_AS_CHAR)
        {
            SwTextNode *pTextNode = rFormatAnchor.GetAnchorNode()->GetTextNode();
            const sal_Int32 nIdx = rFormatAnchor.GetAnchorContentOffset();
            pTextNode->DeleteAttributes( RES_TXTATR_FLYCNT, nIdx, nIdx );
        }
        else
            pFormat->GetDoc().getIDocumentLayoutAccess().DelLayoutFormat(pFormat);
    }

}

uno::Reference< text::XTextRange >  SwXFrame::getAnchor()
{
    SolarMutexGuard aGuard;
    rtl::Reference<SwXTextRange> aRef;
    SwFrameFormat* pFormat = GetFrameFormat();
    if(!pFormat)
        throw uno::RuntimeException();

    const SwFormatAnchor& rAnchor = pFormat->GetAnchor();
    // return an anchor for non-page bound frames
    // and for page bound frames that have a page no == NULL and a content position
    if ((rAnchor.GetAnchorId() != RndStdIds::FLY_AT_PAGE) ||
        (rAnchor.GetAnchorNode() && !rAnchor.GetPageNum()))
    {
        if (rAnchor.GetAnchorId() == RndStdIds::FLY_AT_PARA)
        {   // ensure that SwXTextRange has SwContentIndex
            aRef = SwXTextRange::CreateXTextRange(pFormat->GetDoc(), SwPosition(*rAnchor.GetAnchorNode()), nullptr);
        }
        else
        {
            aRef = SwXTextRange::CreateXTextRange(pFormat->GetDoc(), *rAnchor.GetContentAnchor(), nullptr);
        }
    }

    return aRef;
}

void SwXFrame::ResetDescriptor()
{
    m_bIsDescriptor = false;
    mxStyleData.clear();
    mxStyleFamily.clear();
    m_pProps.reset();
}

void SwXFrame::attachToRange(uno::Reference<text::XTextRange> const& xTextRange,
        SwPaM const*const pCopySource)
{
    SolarMutexGuard aGuard;
    if(!IsDescriptor())
        throw uno::RuntimeException();
    SwXTextRange* pRange = dynamic_cast<SwXTextRange*>(xTextRange.get());
    OTextCursorHelper* pCursor = dynamic_cast<OTextCursorHelper*>(xTextRange.get());

    SwDoc* pDoc = pRange ? &pRange->GetDoc() : pCursor ? pCursor->GetDoc() : nullptr;
    if(!pDoc)
        throw lang::IllegalArgumentException();

    SwUnoInternalPaM aIntPam(*pDoc);
    // this now needs to return TRUE
    ::sw::XTextRangeToSwPaM(aIntPam, xTextRange);

    SwNode& rNode = pDoc->GetNodes().GetEndOfContent();
    SwPaM aPam(rNode);
    aPam.Move( fnMoveBackward, GoInDoc );

    SfxItemSetFixed<RES_GRFATR_BEGIN, RES_GRFATR_END-1> aGrSet(pDoc->GetAttrPool());

    SfxItemSetFixed<
        RES_FRMATR_BEGIN,       RES_FRMATR_END-1,
        RES_UNKNOWNATR_CONTAINER, RES_UNKNOWNATR_CONTAINER,

        // FillAttribute support
        XATTR_FILL_FIRST, XATTR_FILL_LAST,

        SID_ATTR_BORDER_INNER,  SID_ATTR_BORDER_INNER>
            aFrameSet(pDoc->GetAttrPool() );

    // set correct parent to get the XFILL_NONE FillStyle as needed
    aFrameSet.SetParent(&pDoc->GetDfltFrameFormat()->GetAttrSet());

    // no the related items need to be added to the set
    bool bSizeFound;
    if (!m_pProps->AnyToItemSet(*pDoc, aFrameSet, aGrSet, bSizeFound))
        throw lang::IllegalArgumentException();
    // a TextRange is handled separately
    *aPam.GetPoint() = *aIntPam.GetPoint();
    if(aIntPam.HasMark())
    {
        aPam.SetMark();
        *aPam.GetMark() = *aIntPam.GetMark();
    }

    RndStdIds eAnchorId = RndStdIds::FLY_AT_PARA;
    if(const SwFormatAnchor* pItem = aFrameSet.GetItemIfSet(RES_ANCHOR, false) )
    {
        eAnchorId = pItem->GetAnchorId();
        if( RndStdIds::FLY_AT_FLY == eAnchorId &&
            !aPam.GetPointNode().FindFlyStartNode())
        {
            // framebound only where a frame exists
            SwFormatAnchor aAnchor(RndStdIds::FLY_AT_PARA);
            aFrameSet.Put(aAnchor);
        }
        else if ((RndStdIds::FLY_AT_PAGE == eAnchorId) &&
                 0 == pItem->GetPageNum() )
        {
            SwFormatAnchor aAnchor( *pItem );
            aAnchor.SetType(RndStdIds::FLY_AT_CHAR); // convert invalid at-page
            aAnchor.SetAnchor( aPam.GetPoint() );
            aFrameSet.Put(aAnchor);
        }

        if (eAnchorId == RndStdIds::FLY_AT_PAGE)
        {
            sal_Int16 nRelOrient(aFrameSet.Get(RES_HORI_ORIENT).GetRelationOrient());
            if (sw::GetAtPageRelOrientation(nRelOrient, true))
            {
                SAL_WARN("sw.core", "SwXFrame: fixing invalid horizontal RelOrientation for at-page anchor");

                SwFormatHoriOrient item(aFrameSet.Get(RES_HORI_ORIENT));
                item.SetRelationOrient(nRelOrient);
                aFrameSet.Put(item);
            }
        }
    }

    SwFrameFormat *pParentFrameFormat = nullptr;
    if (const uno::Any* pStyle = m_pProps->GetProperty(FN_UNO_FRAME_STYLE_NAME, 0))
        pParentFrameFormat = lcl_GetFrameFormat( *pStyle, *pDoc );

    SwFlyFrameFormat* pFormat = nullptr;
    if( m_eType == FLYCNTTYPE_FRM)
    {
        UnoActionContext aCont(pDoc);
        if (pCopySource)
        {
            std::unique_ptr<SwFormatAnchor> pAnchorItem;
            // the frame is inserted bound to page
            // to prevent conflicts if the to-be-anchored position is part of the to-be-copied text
            if (eAnchorId != RndStdIds::FLY_AT_PAGE)
            {
                pAnchorItem.reset(aFrameSet.Get(RES_ANCHOR).Clone());
                aFrameSet.Put( SwFormatAnchor( RndStdIds::FLY_AT_PAGE, 1 ));
            }

            // park these no longer needed PaMs somewhere safe so MakeFlyAndMove
            // can delete what it likes without any assert these are pointing to
            // that content
            aPam.DeleteMark();
            aIntPam.DeleteMark();
            aIntPam.GetPoint()->Assign(*pDoc->GetNodes()[SwNodeOffset(0)]);
            *aPam.GetPoint() = *aIntPam.GetPoint();

            pFormat = pDoc->MakeFlyAndMove( *pCopySource, aFrameSet,
                           nullptr,
                           pParentFrameFormat );
            if(pAnchorItem && pFormat)
            {
                pFormat->DelFrames();
                pAnchorItem->SetAnchor( pCopySource->Start() );
                SfxItemSetFixed<RES_ANCHOR, RES_ANCHOR> aAnchorSet( pDoc->GetAttrPool() );
                aAnchorSet.Put( std::move(pAnchorItem) );
                pDoc->SetFlyFrameAttr( *pFormat, aAnchorSet );
            }
        }
        else
        {
            pFormat = pDoc->MakeFlySection( RndStdIds::FLY_AT_PARA, aPam.GetPoint(),
                                     &aFrameSet, pParentFrameFormat );
        }
        if(pFormat)
        {
            EndListeningAll();
            m_pFrameFormat = pFormat;
            StartListening(pFormat->GetNotifier());
            if(!m_sName.isEmpty())
                pDoc->SetFlyName(*pFormat, m_sName);
        }
        // wake up the SwXTextFrame
        static_cast<SwXTextFrame*>(this)->SetDoc( m_bIsDescriptor ? m_pDoc : &GetFrameFormat()->GetDoc() );
    }
    else if( m_eType == FLYCNTTYPE_GRF)
    {
        UnoActionContext aActionContext(pDoc);
        Graphic aGraphic;

        // Read graphic URL from the descriptor, if it has any.
        if (const uno::Any* pGraphicURL = m_pProps->GetProperty(FN_UNO_GRAPHIC_URL, 0))
        {
            OUString sGraphicURL;
            uno::Reference<awt::XBitmap> xBitmap;
            if (((*pGraphicURL) >>= sGraphicURL) && !sGraphicURL.isEmpty())
                aGraphic = vcl::graphic::loadFromURL(sGraphicURL);
            else if ((*pGraphicURL) >>= xBitmap)
            {
                uno::Reference<graphic::XGraphic> xGraphic(xBitmap, uno::UNO_QUERY);
                if (xGraphic.is())
                    aGraphic = xGraphic;
            }
        }

        if (const uno::Any* pGraphicAny = m_pProps->GetProperty(FN_UNO_GRAPHIC, 0))
        {
            uno::Reference<graphic::XGraphic> xGraphic;
            (*pGraphicAny) >>= xGraphic;
            aGraphic = Graphic(xGraphic);
        }

        OUString sFilterName;
        if (const uno::Any* pFilterAny = m_pProps->GetProperty(FN_UNO_GRAPHIC_FILTER, 0))
        {
            (*pFilterAny) >>= sFilterName;
        }

        pFormat = pDoc->getIDocumentContentOperations().InsertGraphic(
                    aPam, OUString(), sFilterName, &aGraphic, &aFrameSet, &aGrSet, pParentFrameFormat);
        if (pFormat)
        {
            SwGrfNode *pGrfNd = pDoc->GetNodes()[ pFormat->GetContent().GetContentIdx()
                                        ->GetIndex()+1 ]->GetGrfNode();
            if (pGrfNd)
                pGrfNd->SetChgTwipSize( !bSizeFound );
            m_pFrameFormat = pFormat;
            EndListeningAll();
            StartListening(m_pFrameFormat->GetNotifier());
            if(!m_sName.isEmpty())
                pDoc->SetFlyName(*pFormat, m_sName);

        }
        if (const uno::Any* pSurroundContour = m_pProps->GetProperty(RES_SURROUND, MID_SURROUND_CONTOUR))
            setPropertyValue(UNO_NAME_SURROUND_CONTOUR, *pSurroundContour);
        if (const uno::Any* pContourOutside = m_pProps->GetProperty(RES_SURROUND, MID_SURROUND_CONTOUROUTSIDE))
            setPropertyValue(UNO_NAME_CONTOUR_OUTSIDE, *pContourOutside);
        if (const ::uno::Any* pContourPoly = m_pProps->GetProperty(FN_PARAM_CONTOUR_PP, 0))
            setPropertyValue(UNO_NAME_CONTOUR_POLY_POLYGON, *pContourPoly);
        if (const uno::Any* pPixelContour = m_pProps->GetProperty(FN_UNO_IS_PIXEL_CONTOUR, 0))
            setPropertyValue(UNO_NAME_IS_PIXEL_CONTOUR, *pPixelContour);
        if (const uno::Any* pAutoContour = m_pProps->GetProperty(FN_UNO_IS_AUTOMATIC_CONTOUR, 0))
            setPropertyValue(UNO_NAME_IS_AUTOMATIC_CONTOUR, *pAutoContour);
    }
    else
    {
        const uno::Any* pCLSID = m_pProps->GetProperty(FN_UNO_CLSID, 0);
        const uno::Any* pStreamName = m_pProps->GetProperty(FN_UNO_STREAM_NAME, 0);
        const uno::Any* pEmbeddedObject = m_pProps->GetProperty(FN_EMBEDDED_OBJECT, 0);
        if (!pCLSID && !pStreamName && !pEmbeddedObject)
        {
            throw uno::RuntimeException();
        }
        if(pCLSID)
        {
            OUString aCLSID;
            SvGlobalName aClassName;
            uno::Reference < embed::XEmbeddedObject > xIPObj;
            std::unique_ptr < comphelper::EmbeddedObjectContainer > pCnt;
            if( (*pCLSID) >>= aCLSID )
            {
                if( !aClassName.MakeId( aCLSID ) )
                {
                    throw lang::IllegalArgumentException(u"CLSID invalid"_ustr, nullptr, 0);
                }

                pCnt.reset( new comphelper::EmbeddedObjectContainer );
                OUString aName;

                OUString sDocumentBaseURL = pDoc->GetPersist()->getDocumentBaseURL();
                xIPObj = pCnt->CreateEmbeddedObject(aClassName.GetByteSequence(), aName,
                                                    &sDocumentBaseURL);
            }
            if ( xIPObj.is() )
            {
                UnoActionContext aAction(pDoc);
                pDoc->GetIDocumentUndoRedo().StartUndo(SwUndoId::INSERT, nullptr);

                // tdf#99631 set imported VisibleArea settings of embedded XLSX OLE objects
                if ( m_nDrawAspect == embed::Aspects::MSOLE_CONTENT
                        && m_nVisibleAreaWidth && m_nVisibleAreaHeight )
                {
                    sal_Int64 nAspect = m_nDrawAspect;
                    MapUnit aUnit = VCLUnoHelper::UnoEmbed2VCLMapUnit( xIPObj->getMapUnit( nAspect ) );
                    Size aSize( OutputDevice::LogicToLogic(Size( m_nVisibleAreaWidth, m_nVisibleAreaHeight),
                        MapMode(MapUnit::MapTwip), MapMode(aUnit)));
                    awt::Size aSz;
                    aSz.Width = aSize.Width();
                    aSz.Height = aSize.Height();
                    xIPObj->setVisualAreaSize(m_nDrawAspect, aSz);
                }

                if(!bSizeFound)
                {
                    //TODO/LATER: how do I transport it to the OLENode?
                    sal_Int64 nAspect = m_nDrawAspect;

                    // TODO/LEAN: VisualArea still needs running state
                    (void)svt::EmbeddedObjectRef::TryRunningState( xIPObj );

                    // set parent to get correct VisArea(in case of object needing parent printer)
                    uno::Reference < container::XChild > xChild( xIPObj, uno::UNO_QUERY );
                    SwDocShell* pShell = pDoc->GetDocShell();
                    if ( xChild.is() && pShell )
                        xChild->setParent( pShell->GetModel() );

                    //The Size should be suggested by the OLE server if not manually set
                    MapUnit aRefMap = VCLUnoHelper::UnoEmbed2VCLMapUnit( xIPObj->getMapUnit( nAspect ) );
                    awt::Size aSize;
                    try
                    {
                        aSize = xIPObj->getVisualAreaSize( nAspect );
                    }
                    catch ( embed::NoVisualAreaSizeException& )
                    {
                        // the default size will be set later
                    }

                    Size aSz( aSize.Width, aSize.Height );
                    if ( !aSz.Width() || !aSz.Height() )
                    {
                        aSz.setWidth(5000);
                        aSz.setHeight(5000);
                        aSz = OutputDevice::LogicToLogic(aSz,
                                MapMode(MapUnit::Map100thMM), MapMode(aRefMap));
                    }
                    MapMode aMyMap( MapUnit::MapTwip );
                    aSz = OutputDevice::LogicToLogic(aSz, MapMode(aRefMap), aMyMap);
                    SwFormatFrameSize aFrameSz;
                    aFrameSz.SetSize(aSz);
                    aFrameSet.Put(aFrameSz);
                }
                SwFlyFrameFormat* pFormat2 = nullptr;

                ::svt::EmbeddedObjectRef xObjRef( xIPObj, m_nDrawAspect);
                pFormat2 = pDoc->getIDocumentContentOperations().InsertEmbObject(
                        aPam, xObjRef, &aFrameSet );

                // store main document name to show in the title bar
                if (SwDocShell* pShell = pDoc->GetDocShell())
                {
                    uno::Reference< frame::XTitle > xModelTitle( pShell->GetModel(), css::uno::UNO_QUERY );
                    if( xModelTitle.is() )
                        xIPObj->setContainerName( xModelTitle->getTitle() );
                }

                assert(pFormat2 && "Doc->Insert(notxt) failed.");

                pDoc->GetIDocumentUndoRedo().EndUndo(SwUndoId::INSERT, nullptr);
                m_pFrameFormat = pFormat2;
                EndListeningAll();
                StartListening(m_pFrameFormat->GetNotifier());
                if(!m_sName.isEmpty())
                    pDoc->SetFlyName(*pFormat2, m_sName);
            }
        }
        else if( pStreamName )
        {
            OUString sStreamName;
            (*pStreamName) >>= sStreamName;
            pDoc->GetIDocumentUndoRedo().StartUndo(SwUndoId::INSERT, nullptr);

            SwFlyFrameFormat* pFrameFormat = pDoc->getIDocumentContentOperations().InsertOLE(
                aPam, sStreamName, m_nDrawAspect, &aFrameSet, nullptr);

            // store main document name to show in the title bar
            SwOLENode* pNd = nullptr;
            const SwNodeIndex* pIdx = pFrameFormat->GetContent().GetContentIdx();
            if( pIdx )
            {
                SwNodeIndex aIdx( *pIdx, 1 );
                SwNoTextNode* pNoText = aIdx.GetNode().GetNoTextNode();
                pNd = pNoText->GetOLENode();
            }
            if( pNd )
            {
                uno::Reference < embed::XEmbeddedObject > xObj = pNd->GetOLEObj().GetOleRef();
                SwDocShell* pShell = pDoc->GetDocShell();
                if( xObj.is() && pShell )
                {
                    uno::Reference< frame::XTitle > xModelTitle( pShell->GetModel(), css::uno::UNO_QUERY );
                    if( xModelTitle.is() )
                        xObj->setContainerName( xModelTitle->getTitle() );
                }
            }

            pDoc->GetIDocumentUndoRedo().EndUndo(SwUndoId::INSERT, nullptr);
            m_pFrameFormat = pFrameFormat;
            EndListeningAll();
            StartListening(m_pFrameFormat->GetNotifier());
            if(!m_sName.isEmpty())
                pDoc->SetFlyName(*pFrameFormat, m_sName);
        }
        else if (pEmbeddedObject)
        {
            uno::Reference< embed::XEmbeddedObject > obj;
            (*pEmbeddedObject) >>= obj;
            svt::EmbeddedObjectRef xObj;
            xObj.Assign( obj, embed::Aspects::MSOLE_CONTENT );

            pDoc->GetIDocumentUndoRedo().StartUndo(SwUndoId::INSERT, nullptr);

            // Do not call here container::XChild(obj)->setParent() and
            // pDoc->GetPersist()->GetEmbeddedObjectContainer().InsertEmbeddedObject:
            // they are called indirectly by pDoc->getIDocumentContentOperations().InsertEmbObject
            // below. Calling them twice will add the same object twice to EmbeddedObjectContainer's
            // pImpl->maNameToObjectMap, and then it will misbehave in
            // EmbeddedObjectContainer::StoreAsChildren and SfxObjectShell::SaveCompletedChildren.

            SwFlyFrameFormat* pFrameFormat
                = pDoc->getIDocumentContentOperations().InsertEmbObject(aPam, xObj, &aFrameSet);
            pDoc->GetIDocumentUndoRedo().EndUndo(SwUndoId::INSERT, nullptr);
            m_pFrameFormat = pFrameFormat;
            EndListeningAll();
            StartListening(m_pFrameFormat->GetNotifier());
            if(!m_sName.isEmpty())
                pDoc->SetFlyName(*pFrameFormat, m_sName);
        }
    }
    if( pFormat && pDoc->getIDocumentDrawModelAccess().GetDrawModel() )
        GetOrCreateSdrObject(*pFormat);
    if (const uno::Any* pOrder = m_pProps->GetProperty(FN_UNO_Z_ORDER, 0))
        setPropertyValue(UNO_NAME_Z_ORDER, *pOrder);
    if (const uno::Any* pReplacement = m_pProps->GetProperty(FN_UNO_REPLACEMENT_GRAPHIC, 0))
        setPropertyValue(UNO_NAME_GRAPHIC, *pReplacement);
    // new attribute Title
    if (const uno::Any* pTitle = m_pProps->GetProperty(FN_UNO_TITLE, 0))
    {
        setPropertyValue(UNO_NAME_TITLE, *pTitle);
    }
    // new attribute Description
    if (const uno::Any* pDescription = m_pProps->GetProperty(FN_UNO_DESCRIPTION, 0))
    {
        setPropertyValue(UNO_NAME_DESCRIPTION, *pDescription);
    }

    // For grabbag
    if (const uno::Any* pFrameIntropgrabbagItem = m_pProps->GetProperty(RES_FRMATR_GRABBAG, 0))
    {
        setPropertyValue(UNO_NAME_FRAME_INTEROP_GRAB_BAG, *pFrameIntropgrabbagItem);
    }

    // reset the flag and delete Descriptor pointer
    ResetDescriptor();
}

void SwXFrame::attach(const uno::Reference< text::XTextRange > & xTextRange)
{
    SolarMutexGuard g;

    if(IsDescriptor())
    {
        attachToRange(xTextRange);
        return;
    }

    SwFrameFormat* pFormat = GetFrameFormat();
    if( !pFormat )
        return;

    SwDoc& rDoc = pFormat->GetDoc();
    SwUnoInternalPaM aIntPam(rDoc);
    if (!::sw::XTextRangeToSwPaM(aIntPam, xTextRange))
        throw lang::IllegalArgumentException();

    SfxItemSetFixed<RES_ANCHOR, RES_ANCHOR> aSet( rDoc.GetAttrPool() );
    aSet.SetParent(&pFormat->GetAttrSet());
    SwFormatAnchor aAnchor = aSet.Get(RES_ANCHOR);

    if (aAnchor.GetAnchorId() == RndStdIds::FLY_AS_CHAR)
    {
        throw lang::IllegalArgumentException(
                u"SwXFrame::attach(): re-anchoring AS_CHAR not supported"_ustr,
                *this, 0);
    }

    aAnchor.SetAnchor( aIntPam.Start() );
    aSet.Put(aAnchor);
    rDoc.SetFlyFrameAttr( *pFormat, aSet );
}

awt::Point SwXFrame::getPosition()
{
    throw uno::RuntimeException(u"position cannot be determined with this method"_ustr);
}

void SwXFrame::setPosition(const awt::Point& /*aPosition*/)
{
    throw uno::RuntimeException(u"position cannot be changed with this method"_ustr);
}

awt::Size SwXFrame::getSize()
{
    const ::uno::Any aVal = getPropertyValue(u"Size"_ustr);
    awt::Size const * pRet =  o3tl::doAccess<awt::Size>(aVal);
    return *pRet;
}

void SwXFrame::setSize(const awt::Size& aSize)
{
    const ::uno::Any aVal(&aSize, ::cppu::UnoType<awt::Size>::get());
    setPropertyValue(u"Size"_ustr, aVal);
}

OUString SwXFrame::getShapeType()
{
    return u"FrameShape"_ustr;
}

SwXTextFrame::SwXTextFrame( SwDoc *_pDoc ) :
    SwXTextFrameBaseClass(FLYCNTTYPE_FRM, aSwMapProvider.GetPropertySet(PROPERTY_MAP_TEXT_FRAME), _pDoc ),
    SwXText(nullptr, CursorType::Frame)
{
}

SwXTextFrame::SwXTextFrame(SwFrameFormat& rFormat) :
    SwXTextFrameBaseClass(rFormat, FLYCNTTYPE_FRM, aSwMapProvider.GetPropertySet(PROPERTY_MAP_TEXT_FRAME)),
    SwXText(&rFormat.GetDoc(), CursorType::Frame)
{

}

SwXTextFrame::~SwXTextFrame()
{
}

rtl::Reference<SwXTextFrame>
SwXTextFrame::CreateXTextFrame(SwDoc & rDoc, SwFrameFormat *const pFrameFormat)
{
    return CreateXFrame<SwXTextFrame>(rDoc, pFrameFormat);
}

void SAL_CALL SwXTextFrame::acquire(  )noexcept
{
    SwXFrame::acquire();
}

void SAL_CALL SwXTextFrame::release(  )noexcept
{
    SwXFrame::release();
}

::uno::Any SAL_CALL SwXTextFrame::queryInterface( const uno::Type& aType )
{
    ::uno::Any aRet = SwXFrame::queryInterface(aType);
    if(aRet.getValueType() == cppu::UnoType<void>::get())
        aRet = SwXText::queryInterface(aType);
    if(aRet.getValueType() == cppu::UnoType<void>::get())
        aRet = SwXTextFrameBaseClass::queryInterface(aType);
    return aRet;
}

uno::Sequence< uno::Type > SAL_CALL SwXTextFrame::getTypes(  )
{
    return comphelper::concatSequences(
        SwXTextFrameBaseClass::getTypes(),
        SwXFrame::getTypes(),
        SwXText::getTypes()
    );
}

uno::Sequence< sal_Int8 > SAL_CALL SwXTextFrame::getImplementationId(  )
{
    return css::uno::Sequence<sal_Int8>();
}

uno::Reference< text::XText >  SwXTextFrame::getText()
{
    return this;
}

const SwStartNode *SwXTextFrame::GetStartNode() const
{
    const SwStartNode *pSttNd = nullptr;

    const SwFrameFormat* pFormat = GetFrameFormat();
    if(pFormat)
    {
        const SwFormatContent& rFlyContent = pFormat->GetContent();
        if( rFlyContent.GetContentIdx() )
            pSttNd = rFlyContent.GetContentIdx()->GetNode().GetStartNode();
    }

    return pSttNd;
}

rtl::Reference<SwXTextCursor>  SwXTextFrame::createXTextCursor()
{
    SwFrameFormat* pFormat = GetFrameFormat();
    if(!pFormat)
        throw uno::RuntimeException();

    //save current start node to be able to check if there is content after the table -
    //otherwise the cursor would be in the body text!
    const SwNode& rNode = pFormat->GetContent().GetContentIdx()->GetNode();
    const SwStartNode* pOwnStartNode = rNode.FindStartNodeByType(SwFlyStartNode);

    SwPaM aPam(rNode);
    aPam.Move(fnMoveForward, GoInNode);
    SwTableNode* pTableNode = aPam.GetPointNode().FindTableNode();
    while( pTableNode )
    {
        aPam.GetPoint()->Assign( *pTableNode->EndOfSectionNode() );
        SwContentNode* pCont = SwNodes::GoNext(aPam.GetPoint());
        pTableNode = pCont->FindTableNode();
    }

    const SwStartNode* pNewStartNode =
        aPam.GetPointNode().FindStartNodeByType(SwFlyStartNode);
    if(!pNewStartNode || pNewStartNode != pOwnStartNode)
    {
        throw uno::RuntimeException(u"no text available"_ustr);
    }

    return new SwXTextCursor(
             pFormat->GetDoc(), this, CursorType::Frame, *aPam.GetPoint());
}

rtl::Reference< SwXTextCursor > SwXTextFrame::createXTextCursorByRange(const uno::Reference< text::XTextRange > & aTextPosition)
{
    SwFrameFormat* pFormat = GetFrameFormat();
    if (!pFormat)
        throw uno::RuntimeException();
    SwUnoInternalPaM aPam(*GetDoc());
    if (!::sw::XTextRangeToSwPaM(aPam, aTextPosition))
        throw uno::RuntimeException();
    return createXTextCursorByRangeImpl(*pFormat, aPam);
}

rtl::Reference< SwXTextCursor > SwXTextFrame::createXTextCursorByRangeImpl(
        SwFrameFormat& rFormat,
        SwUnoInternalPaM& rPam)
{
    rtl::Reference< SwXTextCursor > xRef;
    SwNode& rNode = rFormat.GetContent().GetContentIdx()->GetNode();
    if(rPam.GetPointNode().FindFlyStartNode() == rNode.FindFlyStartNode())
    {
        xRef = new SwXTextCursor(rFormat.GetDoc(), this, CursorType::Frame,
                    *rPam.GetPoint(), rPam.GetMark());
    }
    return xRef;
}

uno::Reference< container::XEnumeration >  SwXTextFrame::createEnumeration()
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFormat = GetFrameFormat();
    if(!pFormat)
        return nullptr;
    SwPosition aPos(pFormat->GetContent().GetContentIdx()->GetNode());
    auto pUnoCursor(GetDoc()->CreateUnoCursor(aPos));
    pUnoCursor->Move(fnMoveForward, GoInNode);
    return SwXParagraphEnumeration::Create(this, pUnoCursor, CursorType::Frame);
}

uno::Type  SwXTextFrame::getElementType()
{
    return cppu::UnoType<text::XTextRange>::get();
}

sal_Bool SwXTextFrame::hasElements()
{
    return true;
}

void SwXTextFrame::attach(const uno::Reference< text::XTextRange > & xTextRange)
{
    SwXFrame::attach(xTextRange);
}

uno::Reference< text::XTextRange >  SwXTextFrame::getAnchor()
{
    SolarMutexGuard aGuard;
    return SwXFrame::getAnchor();
}

void SwXTextFrame::dispose()
{
    SolarMutexGuard aGuard;
    SwXFrame::dispose();
}

void SwXTextFrame::addEventListener(const uno::Reference< lang::XEventListener > & aListener)
{
    SwXFrame::addEventListener(aListener);
}

void SwXTextFrame::removeEventListener(const uno::Reference< lang::XEventListener > & aListener)
{
    SwXFrame::removeEventListener(aListener);
}

OUString SwXTextFrame::getImplementationName()
{
    return u"SwXTextFrame"_ustr;
}

sal_Bool SwXTextFrame::supportsService(const OUString& rServiceName)
{
    return cppu::supportsService(this, rServiceName);
}

uno::Sequence< OUString > SwXTextFrame::getSupportedServiceNames()
{
    uno::Sequence < OUString > aRet = SwXFrame::getSupportedServiceNames();
    aRet.realloc(aRet.getLength() + 2);
    OUString* pArray = aRet.getArray();
    pArray[aRet.getLength() - 2] = "com.sun.star.text.TextFrame";
    pArray[aRet.getLength() - 1] = "com.sun.star.text.Text";
    return aRet;
}

uno::Reference<container::XNameReplace > SAL_CALL SwXTextFrame::getEvents()
{
    return new SwFrameEventDescriptor( *this );
}

::uno::Any SwXTextFrame::getPropertyValue(const OUString& rPropertyName)
{
    SolarMutexGuard aGuard;
    ::uno::Any aRet;
    if(rPropertyName == UNO_NAME_START_REDLINE||
            rPropertyName == UNO_NAME_END_REDLINE)
    {
        //redline can only be returned if it's a living object
        if(!IsDescriptor())
            aRet = SwXText::getPropertyValue(rPropertyName);
    }
#ifndef NDEBUG
    else if (rPropertyName == "DbgIsShapesTextFrame")
    {
        aRet <<= SwTextBoxHelper::isTextBox(GetFrameFormat(), RES_FLYFRMFMT)
                 || SwTextBoxHelper::isTextBox(GetFrameFormat(), RES_DRAWFRMFMT);
    }
#endif
    else
        aRet = SwXFrame::getPropertyValue(rPropertyName);
    return aRet;
}

SwXTextGraphicObject::SwXTextGraphicObject( SwDoc *pDoc )
    : SwXTextGraphicObjectBaseClass(FLYCNTTYPE_GRF,
            aSwMapProvider.GetPropertySet(PROPERTY_MAP_TEXT_GRAPHIC), pDoc)
{
}

SwXTextGraphicObject::SwXTextGraphicObject(SwFrameFormat& rFormat)
    : SwXTextGraphicObjectBaseClass(rFormat, FLYCNTTYPE_GRF,
            aSwMapProvider.GetPropertySet(PROPERTY_MAP_TEXT_GRAPHIC))
{
}

SwXTextGraphicObject::~SwXTextGraphicObject()
{
}

rtl::Reference<SwXTextGraphicObject>
SwXTextGraphicObject::CreateXTextGraphicObject(SwDoc & rDoc, SwFrameFormat *const pFrameFormat)
{
    return CreateXFrame<SwXTextGraphicObject>(rDoc, pFrameFormat);
}

OUString SwXTextGraphicObject::getImplementationName()
{
    return u"SwXTextGraphicObject"_ustr;
}

sal_Bool SwXTextGraphicObject::supportsService(const OUString& rServiceName)
{
    return cppu::supportsService(this, rServiceName);
}

uno::Sequence< OUString > SwXTextGraphicObject::getSupportedServiceNames()
{
    uno::Sequence < OUString > aRet = SwXFrame::getSupportedServiceNames();
    aRet.realloc(aRet.getLength() + 1);
    OUString* pArray = aRet.getArray();
    pArray[aRet.getLength() - 1] = "com.sun.star.text.TextGraphicObject";
    return aRet;
}

uno::Reference<container::XNameReplace> SAL_CALL
    SwXTextGraphicObject::getEvents()
{
    return new SwFrameEventDescriptor( *this );
}

SwXTextEmbeddedObject::SwXTextEmbeddedObject( SwDoc *pDoc )
    : SwXTextEmbeddedObjectBaseClass(FLYCNTTYPE_OLE,
            aSwMapProvider.GetPropertySet(PROPERTY_MAP_EMBEDDED_OBJECT), pDoc)
{
}

SwXTextEmbeddedObject::SwXTextEmbeddedObject(SwFrameFormat& rFormat)
    : SwXTextEmbeddedObjectBaseClass(rFormat, FLYCNTTYPE_OLE,
            aSwMapProvider.GetPropertySet(PROPERTY_MAP_EMBEDDED_OBJECT))
{
}

SwXTextEmbeddedObject::~SwXTextEmbeddedObject()
{
}

rtl::Reference<SwXTextEmbeddedObject>
SwXTextEmbeddedObject::CreateXTextEmbeddedObject(SwDoc & rDoc, SwFrameFormat *const pFrameFormat)
{
    return CreateXFrame<SwXTextEmbeddedObject>(rDoc, pFrameFormat);
}

uno::Reference< lang::XComponent >  SwXTextEmbeddedObject::getEmbeddedObject()
{
    uno::Reference<embed::XEmbeddedObject> xObj(getExtendedControlOverEmbeddedObject());
    return xObj.is() ? uno::Reference<lang::XComponent>(xObj->getComponent(), uno::UNO_QUERY) : nullptr;
}

uno::Reference< embed::XEmbeddedObject > SAL_CALL SwXTextEmbeddedObject::getExtendedControlOverEmbeddedObject()
{
    uno::Reference< embed::XEmbeddedObject > xResult;
    SwFrameFormat*   pFormat = GetFrameFormat();
    if(pFormat)
    {
        SwDoc& rDoc = pFormat->GetDoc();
        const SwFormatContent* pCnt = &pFormat->GetContent();
        OSL_ENSURE( pCnt->GetContentIdx() &&
                       rDoc.GetNodes()[ pCnt->GetContentIdx()->
                                        GetIndex() + 1 ]->GetOLENode(), "no OLE-Node?");

        SwOLENode* pOleNode =  rDoc.GetNodes()[ pCnt->GetContentIdx()
                                        ->GetIndex() + 1 ]->GetOLENode();
        xResult = pOleNode->GetOLEObj().GetOleRef();
        if ( svt::EmbeddedObjectRef::TryRunningState( xResult ) )
        {
            // TODO/LATER: the listener registered after client creation should be able to handle scaling, after that the client is not necessary here
            if (SwDocShell* pShell = rDoc.GetDocShell())
                pShell->GetIPClient( svt::EmbeddedObjectRef( xResult, embed::Aspects::MSOLE_CONTENT ) );

            uno::Reference < lang::XComponent > xComp( xResult->getComponent(), uno::UNO_QUERY );
            uno::Reference< util::XModifyBroadcaster >  xBrdcst( xComp, uno::UNO_QUERY);
            uno::Reference< frame::XModel > xModel( xComp, uno::UNO_QUERY);
            if(xBrdcst.is() && xModel.is() && !m_xOLEListener.is())
            {
                m_xOLEListener = new SwXOLEListener(*pFormat, xModel);
                xBrdcst->addModifyListener( m_xOLEListener );
            }
        }
    }
    return xResult;
}

sal_Int64 SAL_CALL SwXTextEmbeddedObject::getAspect()
{
    SwFrameFormat*   pFormat = GetFrameFormat();
    if(pFormat)
    {
        SwDoc& rDoc = pFormat->GetDoc();
        const SwFormatContent* pCnt = &pFormat->GetContent();
        OSL_ENSURE( pCnt->GetContentIdx() &&
                       rDoc.GetNodes()[ pCnt->GetContentIdx()->
                                        GetIndex() + 1 ]->GetOLENode(), "no OLE-Node?");

        return rDoc.GetNodes()[ pCnt->GetContentIdx()->GetIndex() + 1 ]->GetOLENode()->GetAspect();
    }

    return embed::Aspects::MSOLE_CONTENT; // return the default value
}

void SAL_CALL SwXTextEmbeddedObject::setAspect( sal_Int64 nAspect )
{
    SwFrameFormat*   pFormat = GetFrameFormat();
    if(pFormat)
    {
        SwDoc& rDoc = pFormat->GetDoc();
        const SwFormatContent* pCnt = &pFormat->GetContent();
        OSL_ENSURE( pCnt->GetContentIdx() &&
                       rDoc.GetNodes()[ pCnt->GetContentIdx()->
                                        GetIndex() + 1 ]->GetOLENode(), "no OLE-Node?");

        rDoc.GetNodes()[ pCnt->GetContentIdx()->GetIndex() + 1 ]->GetOLENode()->SetAspect( nAspect );
    }
}

uno::Reference< graphic::XGraphic > SAL_CALL SwXTextEmbeddedObject::getReplacementGraphic()
{
    SwFrameFormat*   pFormat = GetFrameFormat();
    if(pFormat)
    {
        SwDoc& rDoc = pFormat->GetDoc();
        const SwFormatContent* pCnt = &pFormat->GetContent();
        OSL_ENSURE( pCnt->GetContentIdx() &&
                       rDoc.GetNodes()[ pCnt->GetContentIdx()->
                                        GetIndex() + 1 ]->GetOLENode(), "no OLE-Node?");

        const Graphic* pGraphic = rDoc.GetNodes()[ pCnt->GetContentIdx()->GetIndex() + 1 ]->GetOLENode()->GetGraphic();
        if ( pGraphic )
            return pGraphic->GetXGraphic();
    }

    return uno::Reference< graphic::XGraphic >();
}

OUString SwXTextEmbeddedObject::getImplementationName()
{
    return u"SwXTextEmbeddedObject"_ustr;
}

sal_Bool SwXTextEmbeddedObject::supportsService(const OUString& rServiceName)
{
    return cppu::supportsService(this, rServiceName);
}

uno::Sequence< OUString > SwXTextEmbeddedObject::getSupportedServiceNames()
{
    uno::Sequence < OUString > aRet = SwXFrame::getSupportedServiceNames();
    aRet.realloc(aRet.getLength() + 1);
    OUString* pArray = aRet.getArray();
    pArray[aRet.getLength() - 1] = "com.sun.star.text.TextEmbeddedObject";
    return aRet;
}

uno::Reference<container::XNameReplace> SAL_CALL
    SwXTextEmbeddedObject::getEvents()
{
    return new SwFrameEventDescriptor( *this );
}

namespace
{
    SwOLENode* lcl_GetOLENode(const SwFormat* pFormat)
    {
        if(!pFormat)
            return nullptr;
        const SwNodeIndex* pIdx(pFormat->GetContent().GetContentIdx());
        if(!pIdx)
            return nullptr;
        const SwNodeIndex aIdx(*pIdx, 1);
        return aIdx.GetNode().GetNoTextNode()->GetOLENode();
    }
}

SwXOLEListener::SwXOLEListener( SwFormat& rOLEFormat, uno::Reference< XModel > xOLE)
    : m_pOLEFormat(&rOLEFormat)
    , m_xOLEModel(std::move(xOLE))
{
    StartListening(m_pOLEFormat->GetNotifier());
}

SwXOLEListener::~SwXOLEListener()
{}

void SwXOLEListener::modified( const lang::EventObject& /*rEvent*/ )
{
    SolarMutexGuard aGuard;
    const auto pNd = lcl_GetOLENode(m_pOLEFormat);
    if(!pNd)
        throw uno::RuntimeException();
    const auto xIP = pNd->GetOLEObj().GetOleRef();
    if(xIP.is())
    {
        sal_Int32 nState = xIP->getCurrentState();
        if(nState == embed::EmbedStates::INPLACE_ACTIVE || nState == embed::EmbedStates::UI_ACTIVE)
            // if the OLE-Node is UI-Active do nothing
            return;
    }
    pNd->SetOLESizeInvalid(true);
    pNd->GetDoc().SetOLEObjModified();
}

void SwXOLEListener::disposing( const lang::EventObject& rEvent )
{
    SolarMutexGuard aGuard;
    uno::Reference<util::XModifyListener> xListener( this );
    uno::Reference<frame::XModel> xModel(rEvent.Source, uno::UNO_QUERY);
    uno::Reference<util::XModifyBroadcaster> xBrdcst(xModel, uno::UNO_QUERY);
    if(!xBrdcst.is())
        return;
    try
    {
        xBrdcst->removeModifyListener(xListener);
    }
    catch(uno::Exception const &)
    {
        OSL_FAIL("OLE Listener couldn't be removed");
    }
}

void SwXOLEListener::Notify( const SfxHint& rHint )
{
    if(rHint.GetId() == SfxHintId::Dying)
    {
        m_xOLEModel = nullptr;
        m_pOLEFormat = nullptr;
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
