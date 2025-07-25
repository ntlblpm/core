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

#include <AccessibleSlideSorterView.hxx>
#include <AccessibleSlideSorterObject.hxx>

#include <SlideSorter.hxx>
#include <controller/SlideSorterController.hxx>
#include <controller/SlsPageSelector.hxx>
#include <controller/SlsFocusManager.hxx>
#include <controller/SlsSelectionManager.hxx>
#include <view/SlideSorterView.hxx>
#include <model/SlideSorterModel.hxx>
#include <model/SlsPageDescriptor.hxx>

#include <ViewShell.hxx>
#include <ViewShellHint.hxx>
#include <sdpage.hxx>
#include <drawdoc.hxx>

#include <sdresid.hxx>
#include <strings.hrc>
#include <com/sun/star/accessibility/AccessibleRole.hpp>
#include <com/sun/star/accessibility/AccessibleEventId.hpp>
#include <com/sun/star/accessibility/AccessibleStateType.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <comphelper/accessibleeventnotifier.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <o3tl/safeint.hxx>
#include <rtl/ref.hxx>
#include <sal/log.hxx>
#include <i18nlangtag/languagetag.hxx>

#include <vcl/settings.hxx>
#include <vcl/svapp.hxx>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::accessibility;

namespace accessibility {

/** Inner implementation class of the AccessibleSlideSorterView.

    Note that some event broadcasting is done asynchronously because
    otherwise it could lead to deadlocks on (at least) some Solaris
    machines.  Probably (but unverified) this can happen on all GTK based
    systems.  The asynchronous broadcasting is just a workaround for a
    poorly understood problem.
*/
class AccessibleSlideSorterView::Implementation
    : public SfxListener
{
public:
    Implementation (
        AccessibleSlideSorterView& rAccessibleSlideSorter,
        ::sd::slidesorter::SlideSorter& rSlideSorter,
        vcl::Window* pWindow);
    virtual ~Implementation() override;

    void RequestUpdateChildren();
    void Clear();
    sal_Int32 GetVisibleChildCount() const;
    AccessibleSlideSorterObject* GetAccessibleChild (sal_Int32 nIndex);
    AccessibleSlideSorterObject* GetVisibleChild (sal_Int32 nIndex);

    void ConnectListeners();
    void ReleaseListeners();
    void Notify (SfxBroadcaster& rBroadcaster, const SfxHint& rHint) override;
    DECL_LINK(WindowEventListener, VclWindowEvent&, void);
    DECL_LINK(SelectionChangeListener, LinkParamNone*, void);
    DECL_LINK(BroadcastSelectionChange, void*, void);
    DECL_LINK(FocusChangeListener, LinkParamNone*, void);
    DECL_LINK(VisibilityChangeListener, LinkParamNone*, void);
    DECL_LINK(UpdateChildrenCallback, void*, void);

    void Activated();
private:
    AccessibleSlideSorterView& mrAccessibleSlideSorter;
    ::sd::slidesorter::SlideSorter& mrSlideSorter;
    typedef ::std::vector<rtl::Reference<AccessibleSlideSorterObject> > PageObjectList;
    PageObjectList maPageObjects;
    sal_Int32 mnFirstVisibleChild;
    sal_Int32 mnLastVisibleChild;
    bool mbListeningToDocument;
    VclPtr<vcl::Window> mpWindow;
    sal_Int32 mnFocusedIndex;
    bool mbModelChangeLocked;
    ImplSVEvent * mnUpdateChildrenUserEventId;
    ImplSVEvent * mnSelectionChangeUserEventId;

    void UpdateChildren();
};

//===== AccessibleSlideSorterView =============================================

AccessibleSlideSorterView::AccessibleSlideSorterView(
    ::sd::slidesorter::SlideSorter& rSlideSorter,
    vcl::Window* pContentWindow)
    : mrSlideSorter(rSlideSorter),
      mpContentWindow(pContentWindow)
{
}

void AccessibleSlideSorterView::Init()
{
    mpImpl.reset(new Implementation(*this,mrSlideSorter,mpContentWindow));
}

AccessibleSlideSorterView::~AccessibleSlideSorterView()
{
}

void AccessibleSlideSorterView::FireAccessibleEvent (
    short nEventId,
    const uno::Any& rOldValue,
    const uno::Any& rNewValue )
{
    NotifyAccessibleEvent(nEventId, rOldValue, rNewValue);
}

void SAL_CALL AccessibleSlideSorterView::disposing()
{
    OAccessible::disposing();

    mpImpl.reset();
}

AccessibleSlideSorterObject* AccessibleSlideSorterView::GetAccessibleChildImplementation (
    sal_Int32 nIndex)
{
    AccessibleSlideSorterObject* pResult = nullptr;
    ::osl::MutexGuard aGuard (m_aMutex);

    if (nIndex>=0 && nIndex<mpImpl->GetVisibleChildCount())
        pResult = mpImpl->GetVisibleChild(nIndex);

    return pResult;
}

//=====  XAccessibleContext  ==================================================

sal_Int64 SAL_CALL AccessibleSlideSorterView::getAccessibleChildCount()
{
    ThrowIfDisposed();
    ::osl::MutexGuard aGuard (m_aMutex);
    return mpImpl->GetVisibleChildCount();
}

Reference<XAccessible > SAL_CALL
    AccessibleSlideSorterView::getAccessibleChild (sal_Int64 nIndex)
{
    ThrowIfDisposed();
    ::osl::MutexGuard aGuard (m_aMutex);

    if (nIndex<0 || nIndex>=mpImpl->GetVisibleChildCount())
        throw lang::IndexOutOfBoundsException();

    return  mpImpl->GetVisibleChild(nIndex);
}

Reference<XAccessible > SAL_CALL AccessibleSlideSorterView::getAccessibleParent()
{
    ThrowIfDisposed();
    const SolarMutexGuard aSolarGuard;
    Reference<XAccessible> xParent;

    if (mpContentWindow != nullptr)
        xParent = mpContentWindow->GetAccessibleParent();

    return xParent;
}

sal_Int64 SAL_CALL AccessibleSlideSorterView::getAccessibleIndexInParent()
{
    OSL_ASSERT(getAccessibleParent().is());
    ThrowIfDisposed();
    const SolarMutexGuard aSolarGuard;
    sal_Int64 nIndexInParent(-1);

    Reference<XAccessibleContext> xParentContext (getAccessibleParent()->getAccessibleContext());
    if (xParentContext.is())
    {
        sal_Int64 nChildCount (xParentContext->getAccessibleChildCount());
        for (sal_Int64 i=0; i<nChildCount; ++i)
            if (xParentContext->getAccessibleChild(i).get()
                    == static_cast<XAccessible*>(this))
            {
                nIndexInParent = i;
                break;
            }
    }

    return nIndexInParent;
}

sal_Int16 SAL_CALL AccessibleSlideSorterView::getAccessibleRole()
{
    ThrowIfDisposed();
    return AccessibleRole::PANEL;
}

OUString SAL_CALL AccessibleSlideSorterView::getAccessibleDescription()
{
    ThrowIfDisposed();
    SolarMutexGuard aGuard;

    return SdResId(SID_SD_A11Y_I_SLIDEVIEW_D);
}

OUString SAL_CALL AccessibleSlideSorterView::getAccessibleName()
{
    ThrowIfDisposed();
    SolarMutexGuard aGuard;

    return SdResId(SID_SD_A11Y_I_SLIDEVIEW_N);
}

Reference<XAccessibleRelationSet> SAL_CALL
    AccessibleSlideSorterView::getAccessibleRelationSet()
{
    return Reference<XAccessibleRelationSet>();
}

sal_Int64 SAL_CALL AccessibleSlideSorterView::getAccessibleStateSet()
{
    ThrowIfDisposed();
    const SolarMutexGuard aSolarGuard;
    sal_Int64 nStateSet = 0;

    nStateSet |= AccessibleStateType::FOCUSABLE;
    nStateSet |= AccessibleStateType::SELECTABLE;
    nStateSet |= AccessibleStateType::ENABLED;
    nStateSet |= AccessibleStateType::ACTIVE;
    nStateSet |= AccessibleStateType::MULTI_SELECTABLE;
    nStateSet |= AccessibleStateType::OPAQUE;
    if (mpContentWindow!=nullptr)
    {
        if (mpContentWindow->IsVisible())
            nStateSet |= AccessibleStateType::VISIBLE;
        if (mpContentWindow->IsReallyVisible())
            nStateSet |= AccessibleStateType::SHOWING;
    }

    return nStateSet;
}

lang::Locale SAL_CALL AccessibleSlideSorterView::getLocale()
{
    ThrowIfDisposed ();
    Reference<XAccessibleContext> xParentContext;
    Reference<XAccessible> xParent (getAccessibleParent());
    if (xParent.is())
        xParentContext = xParent->getAccessibleContext();

    if (xParentContext.is())
        return xParentContext->getLocale();
    else
        // Strange, no parent!  Anyway, return the default locale.
        return Application::GetSettings().GetLanguageTag().getLocale();
}

//===== XAccessibleComponent ==================================================

Reference<XAccessible> SAL_CALL
    AccessibleSlideSorterView::getAccessibleAtPoint (const awt::Point& aPoint)
{
    ThrowIfDisposed();
    Reference<XAccessible> xAccessible;
    const SolarMutexGuard aSolarGuard;

    const Point aTestPoint (aPoint.X, aPoint.Y);
    ::sd::slidesorter::model::SharedPageDescriptor pHitDescriptor (
        mrSlideSorter.GetController().GetPageAt(aTestPoint));
    if (pHitDescriptor)
        xAccessible = mpImpl->GetAccessibleChild(
            (pHitDescriptor->GetPage()->GetPageNum()-1)/2);

    return xAccessible;
}

awt::Rectangle AccessibleSlideSorterView::implGetBounds()
{
    awt::Rectangle aBBox;

    if (mpContentWindow != nullptr)
    {
        const Point aPosition (mpContentWindow->GetPosPixel());
        const Size aSize (mpContentWindow->GetOutputSizePixel());

        aBBox.X = aPosition.X();
        aBBox.Y = aPosition.Y();
        aBBox.Width = aSize.Width();
        aBBox.Height = aSize.Height();
    }

    return aBBox;
}

void SAL_CALL AccessibleSlideSorterView::grabFocus()
{
    ThrowIfDisposed();
    const SolarMutexGuard aSolarGuard;

    if (mpContentWindow)
        mpContentWindow->GrabFocus();
}

sal_Int32 SAL_CALL AccessibleSlideSorterView::getForeground()
{
    ThrowIfDisposed();
    svtools::ColorConfig aColorConfig;
    Color nColor = aColorConfig.GetColorValue( svtools::FONTCOLOR ).nColor;
    return static_cast<sal_Int32>(nColor);
}

sal_Int32 SAL_CALL AccessibleSlideSorterView::getBackground()
{
    ThrowIfDisposed();
    Color nColor = Application::GetSettings().GetStyleSettings().GetWindowColor();
    return sal_Int32(nColor);
}

//===== XAccessibleSelection ==================================================

void SAL_CALL AccessibleSlideSorterView::selectAccessibleChild (sal_Int64 nChildIndex)
{
    ThrowIfDisposed();
    const SolarMutexGuard aSolarGuard;

    if (nChildIndex < 0 || nChildIndex >= getAccessibleChildCount())
        throw lang::IndexOutOfBoundsException();

    AccessibleSlideSorterObject* pChild = mpImpl->GetAccessibleChild(nChildIndex);
    if (pChild == nullptr)
        throw lang::IndexOutOfBoundsException();

    mrSlideSorter.GetController().GetPageSelector().SelectPage(pChild->GetPageNumber());
}

sal_Bool SAL_CALL AccessibleSlideSorterView::isAccessibleChildSelected (sal_Int64 nChildIndex)
{
    ThrowIfDisposed();
    bool bIsSelected = false;
    const SolarMutexGuard aSolarGuard;

    if (nChildIndex < 0 || nChildIndex >= getAccessibleChildCount())
        throw lang::IndexOutOfBoundsException();

    AccessibleSlideSorterObject* pChild = mpImpl->GetAccessibleChild(nChildIndex);
    if (pChild == nullptr)
        throw lang::IndexOutOfBoundsException();

    bIsSelected = mrSlideSorter.GetController().GetPageSelector().IsPageSelected(
        pChild->GetPageNumber());

    return bIsSelected;
}

void SAL_CALL AccessibleSlideSorterView::clearAccessibleSelection()
{
    ThrowIfDisposed();
    const SolarMutexGuard aSolarGuard;

    mrSlideSorter.GetController().GetPageSelector().DeselectAllPages();
}

void SAL_CALL AccessibleSlideSorterView::selectAllAccessibleChildren()
{
    ThrowIfDisposed();
    const SolarMutexGuard aSolarGuard;

    mrSlideSorter.GetController().GetPageSelector().SelectAllPages();
}

sal_Int64 SAL_CALL AccessibleSlideSorterView::getSelectedAccessibleChildCount()
{
    ThrowIfDisposed ();
    const SolarMutexGuard aSolarGuard;
    return mrSlideSorter.GetController().GetPageSelector().GetSelectedPageCount();
}

Reference<XAccessible > SAL_CALL
    AccessibleSlideSorterView::getSelectedAccessibleChild (sal_Int64 nSelectedChildIndex )
{
    ThrowIfDisposed ();
    const SolarMutexGuard aSolarGuard;

    if (nSelectedChildIndex < 0 || nSelectedChildIndex >= getSelectedAccessibleChildCount())
        throw lang::IndexOutOfBoundsException();

    Reference<XAccessible> xChild;

    ::sd::slidesorter::controller::PageSelector& rSelector (
        mrSlideSorter.GetController().GetPageSelector());
    sal_Int32 nPageCount(rSelector.GetPageCount());
    sal_Int32 nSelectedCount = 0;
    for (sal_Int32 i=0; i<nPageCount; i++)
        if (rSelector.IsPageSelected(i))
        {
            if (nSelectedCount == nSelectedChildIndex)
            {
                xChild = mpImpl->GetAccessibleChild(i);
                break;
            }
            ++nSelectedCount;
        }

    if ( ! xChild.is() )
        throw lang::IndexOutOfBoundsException();

    return xChild;
}

void SAL_CALL AccessibleSlideSorterView::deselectAccessibleChild (sal_Int64 nChildIndex)
{
    ThrowIfDisposed();
    const SolarMutexGuard aSolarGuard;

    if (nChildIndex < 0 || nChildIndex >= getAccessibleChildCount())
        throw lang::IndexOutOfBoundsException();

    AccessibleSlideSorterObject* pChild = mpImpl->GetAccessibleChild(nChildIndex);
    if (pChild == nullptr)
        throw lang::IndexOutOfBoundsException();

    mrSlideSorter.GetController().GetPageSelector().DeselectPage(pChild->GetPageNumber());
}

// XServiceInfo
OUString SAL_CALL
       AccessibleSlideSorterView::getImplementationName()
{
    return u"AccessibleSlideSorterView"_ustr;
}

sal_Bool SAL_CALL AccessibleSlideSorterView::supportsService (const OUString& sServiceName)
{
    return cppu::supportsService(this, sServiceName);
}

uno::Sequence< OUString> SAL_CALL
       AccessibleSlideSorterView::getSupportedServiceNames()
{
    ThrowIfDisposed ();

    return uno::Sequence<OUString> {
            u"com.sun.star.accessibility.AccessibleContext"_ustr,
            u"com.sun.star.drawing.AccessibleSlideSorterView"_ustr
    };
}

void AccessibleSlideSorterView::ThrowIfDisposed()
{
    if (rBHelper.bDisposed || rBHelper.bInDispose)
    {
        SAL_WARN("sd", "Calling disposed object. Throwing exception:");
        throw lang::DisposedException (u"object has been already disposed"_ustr,
            static_cast<uno::XWeak*>(this));
    }
}

//===== AccessibleSlideSorterView::Implementation =============================

AccessibleSlideSorterView::Implementation::Implementation (
    AccessibleSlideSorterView& rAccessibleSlideSorter,
    ::sd::slidesorter::SlideSorter& rSlideSorter,
    vcl::Window* pWindow)
    : mrAccessibleSlideSorter(rAccessibleSlideSorter),
      mrSlideSorter(rSlideSorter),
      mnFirstVisibleChild(0),
      mnLastVisibleChild(-1),
      mbListeningToDocument(false),
      mpWindow(pWindow),
      mnFocusedIndex(-1),
      mbModelChangeLocked(false),
      mnUpdateChildrenUserEventId(nullptr),
      mnSelectionChangeUserEventId(nullptr)
{
    ConnectListeners();
    UpdateChildren();
}

AccessibleSlideSorterView::Implementation::~Implementation()
{
    if (mnUpdateChildrenUserEventId != nullptr)
        Application::RemoveUserEvent(mnUpdateChildrenUserEventId);
    if (mnSelectionChangeUserEventId != nullptr)
        Application::RemoveUserEvent(mnSelectionChangeUserEventId);
    ReleaseListeners();
    Clear();
}

void AccessibleSlideSorterView::Implementation::RequestUpdateChildren()
{
    if (mnUpdateChildrenUserEventId == nullptr)
        mnUpdateChildrenUserEventId = Application::PostUserEvent(
            LINK(this, AccessibleSlideSorterView::Implementation,
                 UpdateChildrenCallback));
}

void AccessibleSlideSorterView::Implementation::UpdateChildren()
{
      //By default, all children should be accessible. So here workaround is to make all children visible.
      // MT: This was in UpdateVisibility, which has some similarity, and hg merge automatically has put it here. Correct?!
      // In the IA2 CWS, also setting mnFirst/LastVisibleChild was commented out!
    mnLastVisibleChild = maPageObjects.size();

    if (mbModelChangeLocked)
    {
        // Do nothing right now.  When the flag is reset, this method is
        // called again.
        return;
    }

    const Range aRange (mrSlideSorter.GetView().GetVisiblePageRange());
    mnFirstVisibleChild = aRange.Min();
    mnLastVisibleChild = aRange.Max();

    // Release all children.
    Clear();

    // Create new children for the modified visible range.
    maPageObjects.resize(mrSlideSorter.GetModel().GetPageCount());

    // No Visible children
    if (mnFirstVisibleChild == -1 && mnLastVisibleChild == -1)
        return;

    for (sal_Int32 nIndex(mnFirstVisibleChild); nIndex<=mnLastVisibleChild; ++nIndex)
        GetAccessibleChild(nIndex);
}

void AccessibleSlideSorterView::Implementation::Clear()
{
    for (auto& rxPageObject : maPageObjects)
        if (rxPageObject != nullptr)
        {
            mrAccessibleSlideSorter.FireAccessibleEvent(
                AccessibleEventId::CHILD,
                Any(Reference<XAccessible>(rxPageObject)),
                Any());

            Reference<XComponent> xComponent (Reference<XWeak>(rxPageObject), UNO_QUERY);
            if (xComponent.is())
                xComponent->dispose();
            rxPageObject = nullptr;
        }
    maPageObjects.clear();
}

sal_Int32 AccessibleSlideSorterView::Implementation::GetVisibleChildCount() const
{
    if (mnFirstVisibleChild<=mnLastVisibleChild && mnFirstVisibleChild>=0)
        return mnLastVisibleChild - mnFirstVisibleChild + 1;
    else
        return 0;
}

AccessibleSlideSorterObject* AccessibleSlideSorterView::Implementation::GetVisibleChild (
    sal_Int32 nIndex)
{
    assert(nIndex>=0 && nIndex<GetVisibleChildCount());

    return GetAccessibleChild(nIndex+mnFirstVisibleChild);
}

AccessibleSlideSorterObject* AccessibleSlideSorterView::Implementation::GetAccessibleChild (
    sal_Int32 nIndex)
{
    AccessibleSlideSorterObject* pChild = nullptr;

    if (nIndex>=0 && o3tl::make_unsigned(nIndex)<maPageObjects.size())
    {
        if (maPageObjects[nIndex] == nullptr)
        {
            ::sd::slidesorter::model::SharedPageDescriptor pDescriptor(
                mrSlideSorter.GetModel().GetPageDescriptor(nIndex));
            if (pDescriptor)
            {
                maPageObjects[nIndex] = new AccessibleSlideSorterObject(
                    &mrAccessibleSlideSorter,
                    mrSlideSorter,
                    (pDescriptor->GetPage()->GetPageNum()-1)/2);

                mrAccessibleSlideSorter.FireAccessibleEvent(
                    AccessibleEventId::CHILD,
                    Any(),
                    Any(Reference<XAccessible>(maPageObjects[nIndex])));
            }

        }

        pChild = maPageObjects[nIndex].get();
    }
    else
    {
        OSL_ASSERT(nIndex>=0 && o3tl::make_unsigned(nIndex)<maPageObjects.size());
    }

    return pChild;
}

void AccessibleSlideSorterView::Implementation::ConnectListeners()
{
    StartListening (*mrSlideSorter.GetModel().GetDocument());
    StartListening (mrSlideSorter.GetViewShell());
    mbListeningToDocument = true;

    if (mpWindow != nullptr)
        mpWindow->AddEventListener(
            LINK(this,AccessibleSlideSorterView::Implementation,WindowEventListener));

    mrSlideSorter.GetController().GetSelectionManager()->AddSelectionChangeListener(
        LINK(this,AccessibleSlideSorterView::Implementation,SelectionChangeListener));
    mrSlideSorter.GetController().GetFocusManager().AddFocusChangeListener(
        LINK(this,AccessibleSlideSorterView::Implementation,FocusChangeListener));
    mrSlideSorter.GetView().AddVisibilityChangeListener(
        LINK(this,AccessibleSlideSorterView::Implementation,VisibilityChangeListener));
}

void AccessibleSlideSorterView::Implementation::ReleaseListeners()
{
    mrSlideSorter.GetController().GetFocusManager().RemoveFocusChangeListener(
        LINK(this,AccessibleSlideSorterView::Implementation,FocusChangeListener));
    mrSlideSorter.GetController().GetSelectionManager()->RemoveSelectionChangeListener(
        LINK(this,AccessibleSlideSorterView::Implementation,SelectionChangeListener));
    mrSlideSorter.GetView().RemoveVisibilityChangeListener(
        LINK(this,AccessibleSlideSorterView::Implementation,VisibilityChangeListener));

    if (mpWindow != nullptr)
        mpWindow->RemoveEventListener(
            LINK(this,AccessibleSlideSorterView::Implementation,WindowEventListener));

    if (mbListeningToDocument)
    {
        if (!IsListening(mrSlideSorter.GetViewShell()))
        {   // ??? is it even possible that ConnectListeners is called with no
            // view shell and this one with a view shell?
            StartListening(mrSlideSorter.GetViewShell());
        }
        EndListening (*mrSlideSorter.GetModel().GetDocument());
        mbListeningToDocument = false;
    }
}

void AccessibleSlideSorterView::Implementation::Notify (
    SfxBroadcaster&,
    const SfxHint& rHint)
{
    if (rHint.GetId() == SfxHintId::ThisIsAnSdrHint)
    {
        const SdrHint* pSdrHint = static_cast<const SdrHint*>(&rHint);
        switch (pSdrHint->GetKind())
        {
            case SdrHintKind::PageOrderChange:
                RequestUpdateChildren();
                break;
            default:
                break;
        }
    }
    else if (rHint.GetId() == SfxHintId::SdViewShell)
    {
        auto pViewShellHint = static_cast<const sd::ViewShellHint*>(&rHint);
        switch (pViewShellHint->GetHintId())
        {
            case sd::ViewShellHint::HINT_COMPLEX_MODEL_CHANGE_START:
                mbModelChangeLocked = true;
                break;

            case sd::ViewShellHint::HINT_COMPLEX_MODEL_CHANGE_END:
                mbModelChangeLocked = false;
                RequestUpdateChildren();
                break;
            default:
                break;
        }
    }
}

void AccessibleSlideSorterView::SwitchViewActivated()
{
    // Firstly, set focus to view
    FireAccessibleEvent(AccessibleEventId::STATE_CHANGED,
                    Any(),
                    Any(AccessibleStateType::FOCUSED));

    mpImpl->Activated();
}

void AccessibleSlideSorterView::Implementation::Activated()
{
    mrSlideSorter.GetController().GetFocusManager().ShowFocus();

}

IMPL_LINK(AccessibleSlideSorterView::Implementation, WindowEventListener, VclWindowEvent&, rEvent, void)
{
    switch (rEvent.GetId())
    {
        case VclEventId::WindowMove:
        case VclEventId::WindowResize:
            RequestUpdateChildren();
            break;

        case VclEventId::WindowGetFocus:
        case VclEventId::WindowLoseFocus:
            mrAccessibleSlideSorter.FireAccessibleEvent(
                AccessibleEventId::SELECTION_CHANGED,
                Any(),
                Any());
            break;
        default:
            break;
    }
}

IMPL_LINK_NOARG(AccessibleSlideSorterView::Implementation, SelectionChangeListener, LinkParamNone*, void)
{
    if (mnSelectionChangeUserEventId == nullptr)
        mnSelectionChangeUserEventId = Application::PostUserEvent(
            LINK(this, AccessibleSlideSorterView::Implementation, BroadcastSelectionChange));
}

IMPL_LINK_NOARG(AccessibleSlideSorterView::Implementation, BroadcastSelectionChange, void*, void)
{
    mnSelectionChangeUserEventId = nullptr;
    mrAccessibleSlideSorter.FireAccessibleEvent(
        AccessibleEventId::SELECTION_CHANGED,
        Any(),
        Any());
}

IMPL_LINK_NOARG(AccessibleSlideSorterView::Implementation, FocusChangeListener, LinkParamNone*, void)
{
    sal_Int32 nNewFocusedIndex (
        mrSlideSorter.GetController().GetFocusManager().GetFocusedPageIndex());

    bool bHasFocus = mrSlideSorter.GetController().GetFocusManager().IsFocusShowing();
    if (!bHasFocus)
        nNewFocusedIndex = -1;

    // add a checker whether the focus event is sent out. Only after sent, the mnFocusedIndex should be updated.
    bool bSentFocus = false;
    if (nNewFocusedIndex == mnFocusedIndex)
        return;

    if (mnFocusedIndex >= 0)
    {
        AccessibleSlideSorterObject* pObject = GetAccessibleChild(mnFocusedIndex);
        if (pObject != nullptr)
        {
            pObject->FireAccessibleEvent(
                AccessibleEventId::STATE_CHANGED,
                Any(AccessibleStateType::FOCUSED),
                Any());
            bSentFocus = true;
        }
    }
    if (nNewFocusedIndex >= 0)
    {
        AccessibleSlideSorterObject* pObject = GetAccessibleChild(nNewFocusedIndex);
        if (pObject != nullptr)
        {
            pObject->FireAccessibleEvent(
                AccessibleEventId::STATE_CHANGED,
                Any(),
                Any(AccessibleStateType::FOCUSED));
            bSentFocus = true;
        }
    }
    if (bSentFocus)
        mnFocusedIndex = nNewFocusedIndex;
}

IMPL_LINK_NOARG(AccessibleSlideSorterView::Implementation, UpdateChildrenCallback, void*, void)
{
    mnUpdateChildrenUserEventId = nullptr;
    UpdateChildren();
}

IMPL_LINK_NOARG(AccessibleSlideSorterView::Implementation, VisibilityChangeListener, LinkParamNone*, void)
{
    UpdateChildren();
}

} // end of namespace ::accessibility

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
