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

#include <accelerators/presethandler.hxx>
#include <uiconfiguration/imagemanager.hxx>
#include <uielement/constitemcontainer.hxx>
#include <uielement/rootitemcontainer.hxx>
#include <uielement/uielementtypenames.hxx>
#include <menuconfiguration.hxx>
#include <toolboxconfiguration.hxx>

#include <statusbarconfiguration.hxx>

#include <com/sun/star/ui/UIElementType.hpp>
#include <com/sun/star/ui/ConfigurationEvent.hpp>
#include <com/sun/star/ui/ModuleAcceleratorConfiguration.hpp>
#include <com/sun/star/ui/XModuleUIConfigurationManager2.hpp>
#include <com/sun/star/lang/DisposedException.hpp>
#include <com/sun/star/lang/IllegalAccessException.hpp>
#include <com/sun/star/lang/WrappedTargetRuntimeException.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/embed/ElementModes.hpp>
#include <com/sun/star/embed/InvalidStorageException.hpp>
#include <com/sun/star/embed/StorageWrappedTargetException.hpp>
#include <com/sun/star/embed/XTransactedObject.hpp>
#include <com/sun/star/container/ElementExistException.hpp>
#include <com/sun/star/container/XNameAccess.hpp>
#include <com/sun/star/container/XIndexContainer.hpp>
#include <com/sun/star/io/IOException.hpp>
#include <com/sun/star/io/XStream.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/lang/XComponent.hpp>

#include <comphelper/propertysequence.hxx>
#include <comphelper/sequence.hxx>
#include <cppuhelper/exc_hlp.hxx>
#include <cppuhelper/implbase.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <utility>
#include <vcl/svapp.hxx>
#include <sal/log.hxx>
#include <comphelper/interfacecontainer4.hxx>
#include <comphelper/propertyvalue.hxx>
#include <comphelper/sequenceashashmap.hxx>
#include <comphelper/servicehelper.hxx>
#include <o3tl/string_view.hxx>
#include <memory>
#include <mutex>
#include <string_view>

using namespace css;
using namespace com::sun::star::uno;
using namespace com::sun::star::io;
using namespace com::sun::star::embed;
using namespace com::sun::star::lang;
using namespace com::sun::star::container;
using namespace com::sun::star::beans;
using namespace framework;

constexpr OUStringLiteral RESOURCETYPE_MENUBAR = u"menubar";
constexpr OUStringLiteral RESOURCETYPE_TOOLBAR = u"toolbar";
constexpr OUStringLiteral RESOURCETYPE_STATUSBAR = u"statusbar";
constexpr OUStringLiteral RESOURCETYPE_POPUPMENU = u"popupmenu";

namespace {

class ModuleUIConfigurationManager : public cppu::WeakImplHelper<
                                       css::lang::XServiceInfo,
                                       css::lang::XComponent,
                                       css::ui::XModuleUIConfigurationManager2 >
{
public:
    ModuleUIConfigurationManager(
            const css::uno::Reference< css::uno::XComponentContext >& xServiceManager,
            const css::uno::Sequence< css::uno::Any >& aArguments);

    virtual OUString SAL_CALL getImplementationName() override
    {
        return u"com.sun.star.comp.framework.ModuleUIConfigurationManager"_ustr;
    }

    virtual sal_Bool SAL_CALL supportsService(OUString const & ServiceName) override
    {
        return cppu::supportsService(this, ServiceName);
    }

    virtual css::uno::Sequence<OUString> SAL_CALL getSupportedServiceNames() override
    {
        return {u"com.sun.star.ui.ModuleUIConfigurationManager"_ustr};
    }

    // XComponent
    virtual void SAL_CALL dispose() override;
    virtual void SAL_CALL addEventListener( const css::uno::Reference< css::lang::XEventListener >& xListener ) override;
    virtual void SAL_CALL removeEventListener( const css::uno::Reference< css::lang::XEventListener >& aListener ) override;

    // XUIConfiguration
    virtual void SAL_CALL addConfigurationListener( const css::uno::Reference< css::ui::XUIConfigurationListener >& Listener ) override;
    virtual void SAL_CALL removeConfigurationListener( const css::uno::Reference< css::ui::XUIConfigurationListener >& Listener ) override;

    // XUIConfigurationManager
    virtual void SAL_CALL reset() override;
    virtual css::uno::Sequence< css::uno::Sequence< css::beans::PropertyValue > > SAL_CALL getUIElementsInfo( sal_Int16 ElementType ) override;
    virtual css::uno::Reference< css::container::XIndexContainer > SAL_CALL createSettings(  ) override;
    virtual sal_Bool SAL_CALL hasSettings( const OUString& ResourceURL ) override;
    virtual css::uno::Reference< css::container::XIndexAccess > SAL_CALL getSettings( const OUString& ResourceURL, sal_Bool bWriteable ) override;
    virtual void SAL_CALL replaceSettings( const OUString& ResourceURL, const css::uno::Reference< css::container::XIndexAccess >& aNewData ) override;
    virtual void SAL_CALL removeSettings( const OUString& ResourceURL ) override;
    virtual void SAL_CALL insertSettings( const OUString& NewResourceURL, const css::uno::Reference< css::container::XIndexAccess >& aNewData ) override;
    virtual css::uno::Reference< css::uno::XInterface > SAL_CALL getImageManager() override;
    virtual css::uno::Reference< css::ui::XAcceleratorConfiguration > SAL_CALL getShortCutManager() override;
    virtual css::uno::Reference< css::ui::XAcceleratorConfiguration > SAL_CALL createShortCutManager() override;
    virtual css::uno::Reference< css::uno::XInterface > SAL_CALL getEventsManager() override;

    // XModuleUIConfigurationManager
    virtual sal_Bool SAL_CALL isDefaultSettings( const OUString& ResourceURL ) override;
    virtual css::uno::Reference< css::container::XIndexAccess > SAL_CALL getDefaultSettings( const OUString& ResourceURL ) override;

    // XUIConfigurationPersistence
    virtual void SAL_CALL reload() override;
    virtual void SAL_CALL store() override;
    virtual void SAL_CALL storeToStorage( const css::uno::Reference< css::embed::XStorage >& Storage ) override;
    virtual sal_Bool SAL_CALL isModified() override;
    virtual sal_Bool SAL_CALL isReadOnly() override;

private:
    // private data types
    enum Layer
    {
        LAYER_DEFAULT,
        LAYER_USERDEFINED,
        LAYER_COUNT
    };

    enum NotifyOp
    {
        NotifyOp_Remove,
        NotifyOp_Insert,
        NotifyOp_Replace
    };

    struct UIElementInfo
    {
        UIElementInfo( OUString _aResourceURL, OUString _aUIName ) :
            aResourceURL(std::move( _aResourceURL)), aUIName(std::move( _aUIName )) {}
        OUString   aResourceURL;
        OUString   aUIName;
    };

    struct UIElementData
    {
        UIElementData() : bModified( false ), bDefault( true ), bDefaultNode( true ) {};

        OUString aResourceURL;
        OUString aName;
        bool          bModified;        // has been changed since last storing
        bool          bDefault;         // default settings
        bool          bDefaultNode;     // this is a default layer element data
        css::uno::Reference< css::container::XIndexAccess > xSettings;
    };

    typedef std::unordered_map< OUString, UIElementData > UIElementDataHashMap;

    struct UIElementType
    {
        UIElementType() : bModified( false ),
                          bLoaded( false ),
                          nElementType( css::ui::UIElementType::UNKNOWN ) {}

        bool                                                              bModified;
        bool                                                              bLoaded;
        sal_Int16                                                         nElementType;
        UIElementDataHashMap                                              aElementsHashMap;
        css::uno::Reference< css::embed::XStorage > xStorage;
    };

    typedef std::vector< UIElementType > UIElementTypesVector;
    typedef std::vector< css::ui::ConfigurationEvent > ConfigEventNotifyContainer;
    typedef std::unordered_map< OUString, UIElementInfo > UIElementInfoHashMap;

    void            impl_Initialize();
    void            implts_notifyContainerListener( const css::ui::ConfigurationEvent& aEvent, NotifyOp eOp );
    void            impl_fillSequenceWithElementTypeInfo( UIElementInfoHashMap& aUIElementInfoCollection, sal_Int16 nElementType );
    void            impl_preloadUIElementTypeList( Layer eLayer, sal_Int16 nElementType );
    UIElementData*  impl_findUIElementData( const OUString& aResourceURL, sal_Int16 nElementType, bool bLoad = true );
    void            impl_requestUIElementData( sal_Int16 nElementType, Layer eLayer, UIElementData& aUIElementData );
    void            impl_storeElementTypeData( const css::uno::Reference< css::embed::XStorage >& xStorage, UIElementType& rElementType, bool bResetModifyState = true );
    void            impl_resetElementTypeData( UIElementType& rUserElementType, UIElementType const & rDefaultElementType, ConfigEventNotifyContainer& rRemoveNotifyContainer, ConfigEventNotifyContainer& rReplaceNotifyContainer );
    void            impl_reloadElementTypeData( UIElementType& rUserElementType, UIElementType const & rDefaultElementType, ConfigEventNotifyContainer& rRemoveNotifyContainer, ConfigEventNotifyContainer& rReplaceNotifyContainer );

    UIElementTypesVector                                      m_aUIElements[LAYER_COUNT];
    std::unique_ptr<PresetHandler>                            m_pStorageHandler[css::ui::UIElementType::COUNT];
    css::uno::Reference< css::embed::XStorage >               m_xDefaultConfigStorage;
    css::uno::Reference< css::embed::XStorage >               m_xUserConfigStorage;
    bool                                                      m_bReadOnly;
    bool                                                      m_bModified;
    bool                                                      m_bDisposed;
    OUString                                                  m_aXMLPostfix;
    OUString                                                  m_aPropUIName;
    OUString                                                  m_aModuleIdentifier;
    css::uno::Reference< css::embed::XTransactedObject >      m_xUserRootCommit;
    css::uno::Reference< css::uno::XComponentContext >        m_xContext;
    std::mutex                                                m_mutex;
    comphelper::OInterfaceContainerHelper4<css::lang::XEventListener> m_aEventListeners;
    comphelper::OInterfaceContainerHelper4<css::ui::XUIConfigurationListener> m_aConfigListeners;
    rtl::Reference< ImageManager >                            m_xModuleImageManager;
    css::uno::Reference< css::ui::XAcceleratorConfiguration > m_xModuleAcceleratorManager;
};

// important: The order and position of the elements must match the constant
// definition of "css::ui::UIElementType"
constexpr std::u16string_view UIELEMENTTYPENAMES[] =
{
    u"",  // Dummy value for unknown!
    u"" UIELEMENTTYPE_MENUBAR_NAME,
    u"" UIELEMENTTYPE_POPUPMENU_NAME,
    u"" UIELEMENTTYPE_TOOLBAR_NAME,
    u"" UIELEMENTTYPE_STATUSBAR_NAME,
    u"" UIELEMENTTYPE_FLOATINGWINDOW_NAME,
    u"" UIELEMENTTYPE_PROGRESSBAR_NAME,
    u"" UIELEMENTTYPE_TOOLPANEL_NAME
};

constexpr std::u16string_view RESOURCEURL_PREFIX = u"private:resource/";

sal_Int16 RetrieveTypeFromResourceURL( std::u16string_view aResourceURL )
{

    if (( o3tl::starts_with(aResourceURL, RESOURCEURL_PREFIX ) ) &&
        ( aResourceURL.size() > RESOURCEURL_PREFIX.size() ))
    {
        std::u16string_view aTmpStr = aResourceURL.substr( RESOURCEURL_PREFIX.size() );
        size_t nIndex = aTmpStr.find( '/' );
        if (( nIndex > 0 ) &&  ( aTmpStr.size() > nIndex ))
        {
            std::u16string_view aTypeStr( aTmpStr.substr( 0, nIndex ));
            for ( int i = 0; i < ui::UIElementType::COUNT; i++ )
            {
                if ( aTypeStr == UIELEMENTTYPENAMES[i] )
                    return sal_Int16( i );
            }
        }
    }

    return ui::UIElementType::UNKNOWN;
}

OUString RetrieveNameFromResourceURL( std::u16string_view aResourceURL )
{
    if (( o3tl::starts_with(aResourceURL, RESOURCEURL_PREFIX ) ) &&
        ( aResourceURL.size() > RESOURCEURL_PREFIX.size() ))
    {
        size_t nIndex = aResourceURL.rfind( '/' );

        if ( nIndex > 0 && nIndex != std::u16string_view::npos && (( nIndex+1 ) < aResourceURL.size()) )
            return OUString(aResourceURL.substr( nIndex+1 ));
    }

    return OUString();
}

void ModuleUIConfigurationManager::impl_fillSequenceWithElementTypeInfo( UIElementInfoHashMap& aUIElementInfoCollection, sal_Int16 nElementType )
{
    // preload list of element types on demand
    impl_preloadUIElementTypeList( LAYER_USERDEFINED, nElementType );
    impl_preloadUIElementTypeList( LAYER_DEFAULT, nElementType );

    UIElementDataHashMap& rUserElements = m_aUIElements[LAYER_USERDEFINED][nElementType].aElementsHashMap;

    OUString aCustomUrlPrefix( u"custom_"_ustr );
    for (auto const& userElement : rUserElements)
    {
        sal_Int32 nIndex = userElement.second.aResourceURL.indexOf( aCustomUrlPrefix, RESOURCEURL_PREFIX.size() );
        if ( nIndex > static_cast<sal_Int32>(RESOURCEURL_PREFIX.size()) )
        {
            // Performance: Retrieve user interface name only for custom user interface elements.
            // It's only used by them!
            UIElementData* pDataSettings = impl_findUIElementData( userElement.second.aResourceURL, nElementType );
            if ( pDataSettings )
            {
                // Retrieve user interface name from XPropertySet interface
                OUString aUIName;
                Reference< XPropertySet > xPropSet( pDataSettings->xSettings, UNO_QUERY );
                if ( xPropSet.is() )
                {
                    Any a = xPropSet->getPropertyValue( m_aPropUIName );
                    a >>= aUIName;
                }

                UIElementInfo aInfo( userElement.second.aResourceURL, aUIName );
                aUIElementInfoCollection.emplace( userElement.second.aResourceURL, aInfo );
            }
        }
        else
        {
            // The user interface name for standard user interface elements is stored in the WindowState.xcu file
            UIElementInfo aInfo( userElement.second.aResourceURL, OUString() );
            aUIElementInfoCollection.emplace( userElement.second.aResourceURL, aInfo );
        }
    }

    UIElementDataHashMap& rDefaultElements = m_aUIElements[LAYER_DEFAULT][nElementType].aElementsHashMap;

    for (auto const& defaultElement : rDefaultElements)
    {
        UIElementInfoHashMap::const_iterator pIterInfo = aUIElementInfoCollection.find( defaultElement.second.aResourceURL );
        if ( pIterInfo == aUIElementInfoCollection.end() )
        {
            sal_Int32 nIndex = defaultElement.second.aResourceURL.indexOf( aCustomUrlPrefix, RESOURCEURL_PREFIX.size() );
            if ( nIndex > static_cast<sal_Int32>(RESOURCEURL_PREFIX.size()) )
            {
                // Performance: Retrieve user interface name only for custom user interface elements.
                // It's only used by them!
                UIElementData* pDataSettings = impl_findUIElementData( defaultElement.second.aResourceURL, nElementType );
                if ( pDataSettings )
                {
                    // Retrieve user interface name from XPropertySet interface
                    OUString aUIName;
                    Reference< XPropertySet > xPropSet( pDataSettings->xSettings, UNO_QUERY );
                    if ( xPropSet.is() )
                    {
                        Any a = xPropSet->getPropertyValue( m_aPropUIName );
                        a >>= aUIName;
                    }
                    UIElementInfo aInfo( defaultElement.second.aResourceURL, aUIName );
                    aUIElementInfoCollection.emplace( defaultElement.second.aResourceURL, aInfo );
                }
            }
            else
            {
                // The user interface name for standard user interface elements is stored in the WindowState.xcu file
                UIElementInfo aInfo( defaultElement.second.aResourceURL, OUString() );
                aUIElementInfoCollection.emplace( defaultElement.second.aResourceURL, aInfo );
            }
        }
    }
}

void ModuleUIConfigurationManager::impl_preloadUIElementTypeList( Layer eLayer, sal_Int16 nElementType )
{
    UIElementType& rElementTypeData = m_aUIElements[eLayer][nElementType];

    if ( rElementTypeData.bLoaded )
        return;

    Reference< XStorage > xElementTypeStorage = rElementTypeData.xStorage;
    if ( !xElementTypeStorage.is() )
        return;

    OUString aResURLPrefix =
        OUString::Concat(RESOURCEURL_PREFIX) +
        UIELEMENTTYPENAMES[ nElementType ] +
        "/";

    UIElementDataHashMap& rHashMap = rElementTypeData.aElementsHashMap;
    const Sequence< OUString > aUIElementNames = xElementTypeStorage->getElementNames();
    for ( OUString const & rElementName : aUIElementNames )
    {
        UIElementData aUIElementData;

        // Resource name must be without ".xml"
        sal_Int32 nIndex = rElementName.lastIndexOf( '.' );
        if (( nIndex > 0 ) && ( nIndex < rElementName.getLength() ))
        {
            std::u16string_view aExtension( rElementName.subView( nIndex+1 ));
            std::u16string_view aUIElementName( rElementName.subView( 0, nIndex ));

            if (!aUIElementName.empty() &&
                ( o3tl::equalsIgnoreAsciiCase(aExtension, u"xml")))
            {
                aUIElementData.aResourceURL = aResURLPrefix + aUIElementName;
                aUIElementData.aName        = rElementName;

                if ( eLayer == LAYER_USERDEFINED )
                {
                    aUIElementData.bModified    = false;
                    aUIElementData.bDefault     = false;
                    aUIElementData.bDefaultNode = false;
                }

                // Create std::unordered_map entries for all user interface elements inside the storage. We don't load the
                // settings to speed up the process.
                rHashMap.emplace( aUIElementData.aResourceURL, aUIElementData );
            }
        }
        rElementTypeData.bLoaded = true;
    }

}

void ModuleUIConfigurationManager::impl_requestUIElementData( sal_Int16 nElementType, Layer eLayer, UIElementData& aUIElementData )
{
    UIElementType& rElementTypeData = m_aUIElements[eLayer][nElementType];

    Reference< XStorage > xElementTypeStorage = rElementTypeData.xStorage;
    if ( xElementTypeStorage.is() && !aUIElementData.aName.isEmpty() )
    {
        try
        {
            Reference< XStream > xStream = xElementTypeStorage->openStreamElement( aUIElementData.aName, ElementModes::READ );
            Reference< XInputStream > xInputStream = xStream->getInputStream();

            if ( xInputStream.is() )
            {
                switch ( nElementType )
                {
                    case css::ui::UIElementType::UNKNOWN:
                    break;

                    case css::ui::UIElementType::MENUBAR:
                    case css::ui::UIElementType::POPUPMENU:
                    {
                        try
                        {
                            MenuConfiguration aMenuCfg( m_xContext );
                            Reference< XIndexAccess > xContainer( aMenuCfg.CreateMenuBarConfigurationFromXML( xInputStream ));
                            auto pRootItemContainer = dynamic_cast<RootItemContainer*>( xContainer.get() );
                            if ( pRootItemContainer )
                                aUIElementData.xSettings = new ConstItemContainer( pRootItemContainer, true );
                            else
                                aUIElementData.xSettings = new ConstItemContainer( xContainer, true );
                            return;
                        }
                        catch ( const css::lang::WrappedTargetException& )
                        {
                        }
                    }
                    break;

                    case css::ui::UIElementType::TOOLBAR:
                    {
                        try
                        {
                            Reference< XIndexContainer > xIndexContainer( new RootItemContainer() );
                            ToolBoxConfiguration::LoadToolBox( m_xContext, xInputStream, xIndexContainer );
                            auto pRootItemContainer = dynamic_cast<RootItemContainer*>( xIndexContainer.get() );
                            aUIElementData.xSettings = new ConstItemContainer( pRootItemContainer, true );
                            return;
                        }
                        catch ( const css::lang::WrappedTargetException& )
                        {
                        }

                        break;
                    }

                    case css::ui::UIElementType::STATUSBAR:
                    {
                        try
                        {
                            Reference< XIndexContainer > xIndexContainer( new RootItemContainer() );
                            StatusBarConfiguration::LoadStatusBar( m_xContext, xInputStream, xIndexContainer );
                            auto pRootItemContainer = dynamic_cast<RootItemContainer*>( xIndexContainer.get() );
                            aUIElementData.xSettings = new ConstItemContainer( pRootItemContainer, true );
                            return;
                        }
                        catch ( const css::lang::WrappedTargetException& )
                        {
                        }

                        break;
                    }

                    case css::ui::UIElementType::FLOATINGWINDOW:
                    {
                        break;
                    }
                }
            }
        }
        catch ( const css::embed::InvalidStorageException& )
        {
        }
        catch ( const css::lang::IllegalArgumentException& )
        {
        }
        catch ( const css::io::IOException& )
        {
        }
        catch ( const css::embed::StorageWrappedTargetException& )
        {
        }
    }

    // At least we provide an empty settings container!
    aUIElementData.xSettings = new ConstItemContainer();
}

ModuleUIConfigurationManager::UIElementData*  ModuleUIConfigurationManager::impl_findUIElementData( const OUString& aResourceURL, sal_Int16 nElementType, bool bLoad )
{
    // preload list of element types on demand
    impl_preloadUIElementTypeList( LAYER_USERDEFINED, nElementType );
    impl_preloadUIElementTypeList( LAYER_DEFAULT, nElementType );

    // first try to look into our user-defined vector/unordered_map combination
    UIElementDataHashMap& rUserHashMap = m_aUIElements[LAYER_USERDEFINED][nElementType].aElementsHashMap;
    UIElementDataHashMap::iterator pIter = rUserHashMap.find( aResourceURL );
    if ( pIter != rUserHashMap.end() )
    {
        // Default data settings data must be retrieved from the default layer!
        if ( !pIter->second.bDefault )
        {
            if ( !pIter->second.xSettings.is() && bLoad )
                impl_requestUIElementData( nElementType, LAYER_USERDEFINED, pIter->second );
            return &(pIter->second);
        }
    }

    // Not successful, we have to look into our default vector/unordered_map combination
    UIElementDataHashMap& rDefaultHashMap = m_aUIElements[LAYER_DEFAULT][nElementType].aElementsHashMap;
    pIter = rDefaultHashMap.find( aResourceURL );
    if ( pIter != rDefaultHashMap.end() )
    {
        if ( !pIter->second.xSettings.is() && bLoad )
            impl_requestUIElementData( nElementType, LAYER_DEFAULT, pIter->second );
        return &(pIter->second);
    }

    // Nothing has been found!
    return nullptr;
}

void ModuleUIConfigurationManager::impl_storeElementTypeData( const Reference< XStorage >& xStorage, UIElementType& rElementType, bool bResetModifyState )
{
    UIElementDataHashMap& rHashMap          = rElementType.aElementsHashMap;

    for (auto & elem : rHashMap)
    {
        UIElementData& rElement = elem.second;
        if ( rElement.bModified )
        {
            if ( rElement.bDefault )
            {
                xStorage->removeElement( rElement.aName );
                rElement.bModified = false; // mark as not modified
            }
            else
            {
                Reference< XStream > xStream = xStorage->openStreamElement( rElement.aName, ElementModes::WRITE|ElementModes::TRUNCATE );
                Reference< XOutputStream > xOutputStream( xStream->getOutputStream() );

                if ( xOutputStream.is() )
                {
                    switch( rElementType.nElementType )
                    {
                        case css::ui::UIElementType::MENUBAR:
                        case css::ui::UIElementType::POPUPMENU:
                        {
                            try
                            {
                                MenuConfiguration aMenuCfg( m_xContext );
                                aMenuCfg.StoreMenuBarConfigurationToXML(
                                    rElement.xSettings, xOutputStream, rElementType.nElementType == css::ui::UIElementType::MENUBAR );
                            }
                            catch ( const css::lang::WrappedTargetException& )
                            {
                            }
                        }
                        break;

                        case css::ui::UIElementType::TOOLBAR:
                        {
                            try
                            {
                                ToolBoxConfiguration::StoreToolBox( m_xContext, xOutputStream, rElement.xSettings );
                            }
                            catch ( const css::lang::WrappedTargetException& )
                            {
                            }
                        }
                        break;

                        case css::ui::UIElementType::STATUSBAR:
                        {
                            try
                            {
                                StatusBarConfiguration::StoreStatusBar( m_xContext, xOutputStream, rElement.xSettings );
                            }
                            catch ( const css::lang::WrappedTargetException& )
                            {
                            }
                        }
                        break;

                        default:
                        break;
                    }
                }

                // mark as not modified if we store to our own storage
                if ( bResetModifyState )
                    rElement.bModified = false;
            }
        }
    }

    // commit element type storage
    Reference< XTransactedObject > xTransactedObject( xStorage, UNO_QUERY );
    if ( xTransactedObject.is() )
        xTransactedObject->commit();

    // mark UIElementType as not modified if we store to our own storage
    if ( bResetModifyState )
        rElementType.bModified = false;
}

// This is only allowed to be called on the LAYER_USER_DEFINED!
void ModuleUIConfigurationManager::impl_resetElementTypeData(
    UIElementType& rUserElementType,
    UIElementType const & rDefaultElementType,
    ConfigEventNotifyContainer& rRemoveNotifyContainer,
    ConfigEventNotifyContainer& rReplaceNotifyContainer )
{
    UIElementDataHashMap& rHashMap          = rUserElementType.aElementsHashMap;

    Reference< XUIConfigurationManager > xThis(this);
    Reference< XInterface >  xIfac( xThis, UNO_QUERY );
    sal_Int16 nType = rUserElementType.nElementType;

    // Make copies of the event structures to be thread-safe. We have to unlock our mutex before calling
    // our listeners!
    for (auto & elem : rHashMap)
    {
        UIElementData& rElement = elem.second;
        if ( !rElement.bDefault )
        {
            if ( rDefaultElementType.xStorage->hasByName( rElement.aName ))
            {
                // Replace settings with data from default layer
                Reference< XIndexAccess > xOldSettings( rElement.xSettings );
                impl_requestUIElementData( nType, LAYER_DEFAULT, rElement );

                ui::ConfigurationEvent aReplaceEvent;
                aReplaceEvent.ResourceURL = rElement.aResourceURL;
                aReplaceEvent.Accessor <<= xThis;
                aReplaceEvent.Source = xIfac;
                aReplaceEvent.ReplacedElement <<= xOldSettings;
                aReplaceEvent.Element <<= rElement.xSettings;

                rReplaceNotifyContainer.push_back( aReplaceEvent );

                // Mark element as default and not modified. That means "not active"
                // in the user layer anymore.
                rElement.bModified = false;
                rElement.bDefault  = true;
            }
            else
            {
                // Remove user-defined settings from user layer
                ui::ConfigurationEvent aEvent;
                aEvent.ResourceURL = rElement.aResourceURL;
                aEvent.Accessor <<= xThis;
                aEvent.Source = xIfac;
                aEvent.Element <<= rElement.xSettings;

                rRemoveNotifyContainer.push_back( aEvent );

                // Mark element as default and not modified. That means "not active"
                // in the user layer anymore.
                rElement.bModified = false;
                rElement.bDefault  = true;
            }
        }
    }

    // Remove all settings from our user interface elements
    rHashMap.clear();
}

void ModuleUIConfigurationManager::impl_reloadElementTypeData(
    UIElementType&              rUserElementType,
    UIElementType const &       rDefaultElementType,
    ConfigEventNotifyContainer& rRemoveNotifyContainer,
    ConfigEventNotifyContainer& rReplaceNotifyContainer )
{
    UIElementDataHashMap& rHashMap          = rUserElementType.aElementsHashMap;

    Reference< XUIConfigurationManager > xThis(this);
    Reference< XInterface > xIfac( xThis, UNO_QUERY );
    sal_Int16 nType = rUserElementType.nElementType;

    for (auto & elem : rHashMap)
    {
        UIElementData& rElement = elem.second;
        if ( rElement.bModified )
        {
            if ( rUserElementType.xStorage->hasByName( rElement.aName ))
            {
                // Replace settings with data from user layer
                Reference< XIndexAccess > xOldSettings( rElement.xSettings );

                impl_requestUIElementData( nType, LAYER_USERDEFINED, rElement );

                ui::ConfigurationEvent aReplaceEvent;

                aReplaceEvent.ResourceURL = rElement.aResourceURL;
                aReplaceEvent.Accessor <<= xThis;
                aReplaceEvent.Source = xIfac;
                aReplaceEvent.ReplacedElement <<= xOldSettings;
                aReplaceEvent.Element <<= rElement.xSettings;
                rReplaceNotifyContainer.push_back( aReplaceEvent );

                rElement.bModified = false;
            }
            else if ( rDefaultElementType.xStorage->hasByName( rElement.aName ))
            {
                // Replace settings with data from default layer
                Reference< XIndexAccess > xOldSettings( rElement.xSettings );

                impl_requestUIElementData( nType, LAYER_DEFAULT, rElement );

                ui::ConfigurationEvent aReplaceEvent;

                aReplaceEvent.ResourceURL = rElement.aResourceURL;
                aReplaceEvent.Accessor <<= xThis;
                aReplaceEvent.Source = xIfac;
                aReplaceEvent.ReplacedElement <<= xOldSettings;
                aReplaceEvent.Element <<= rElement.xSettings;
                rReplaceNotifyContainer.push_back( aReplaceEvent );

                // Mark element as default and not modified. That means "not active"
                // in the user layer anymore.
                rElement.bModified = false;
                rElement.bDefault  = true;
            }
            else
            {
                // Element settings are not in any storage => remove
                ui::ConfigurationEvent aRemoveEvent;

                aRemoveEvent.ResourceURL = rElement.aResourceURL;
                aRemoveEvent.Accessor <<= xThis;
                aRemoveEvent.Source = xIfac;
                aRemoveEvent.Element <<= rElement.xSettings;

                rRemoveNotifyContainer.push_back( aRemoveEvent );

                // Mark element as default and not modified. That means "not active"
                // in the user layer anymore.
                rElement.bModified = false;
                rElement.bDefault  = true;
            }
        }
    }

    rUserElementType.bModified = false;
}

void ModuleUIConfigurationManager::impl_Initialize()
{
    // Initialize the top-level structures with the storage data
    if ( m_xUserConfigStorage.is() )
    {
        // Try to access our module sub folder
        for ( sal_Int16 i = 1; i < css::ui::UIElementType::COUNT;
              i++ )
        {
            Reference< XStorage > xElementTypeStorage;
            try
            {
                if ( m_pStorageHandler[i] )
                    xElementTypeStorage = m_pStorageHandler[i]->getWorkingStorageUser();
            }
            catch ( const css::container::NoSuchElementException& )
            {
            }
            catch ( const css::embed::InvalidStorageException& )
            {
            }
            catch ( const css::lang::IllegalArgumentException& )
            {
            }
            catch ( const css::io::IOException& )
            {
            }
            catch ( const css::embed::StorageWrappedTargetException& )
            {
            }

            m_aUIElements[LAYER_USERDEFINED][i].nElementType = i;
            m_aUIElements[LAYER_USERDEFINED][i].bModified = false;
            m_aUIElements[LAYER_USERDEFINED][i].xStorage = std::move(xElementTypeStorage);
        }
    }

    if ( !m_xDefaultConfigStorage.is() )
        return;

    Reference< XNameAccess > xNameAccess( m_xDefaultConfigStorage, UNO_QUERY_THROW );

    // Try to access our module sub folder
    for ( sal_Int16 i = 1; i < css::ui::UIElementType::COUNT;
          i++ )
    {
        Reference< XStorage > xElementTypeStorage;
        try
        {
            const OUString sName( UIELEMENTTYPENAMES[i] );
            if( xNameAccess->hasByName( sName ) )
                xNameAccess->getByName( sName ) >>= xElementTypeStorage;
        }
        catch ( const css::container::NoSuchElementException& )
        {
        }

        m_aUIElements[LAYER_DEFAULT][i].nElementType = i;
        m_aUIElements[LAYER_DEFAULT][i].bModified = false;
        m_aUIElements[LAYER_DEFAULT][i].xStorage = std::move(xElementTypeStorage);
    }
}

ModuleUIConfigurationManager::ModuleUIConfigurationManager(
        const Reference< XComponentContext >& xContext,
        const css::uno::Sequence< css::uno::Any >& aArguments)
    : m_bReadOnly( true )
    , m_bModified( false )
    , m_bDisposed( false )
    , m_aXMLPostfix( u".xml"_ustr )
    , m_aPropUIName( u"UIName"_ustr )
    , m_xContext( xContext )
{
    // Make sure we have a default initialized entry for every layer and user interface element type!
    // The following code depends on this!
    m_aUIElements[LAYER_DEFAULT].resize( css::ui::UIElementType::COUNT );
    m_aUIElements[LAYER_USERDEFINED].resize( css::ui::UIElementType::COUNT );

    SolarMutexGuard g;

    OUString aModuleShortName;
    if( aArguments.getLength() == 2 && (aArguments[0] >>= aModuleShortName) && (aArguments[1] >>= m_aModuleIdentifier))
    {
    }
    else
    {
        ::comphelper::SequenceAsHashMap lArgs(aArguments);
        aModuleShortName  = lArgs.getUnpackedValueOrDefault(u"ModuleShortName"_ustr, OUString());
        m_aModuleIdentifier = lArgs.getUnpackedValueOrDefault(u"ModuleIdentifier"_ustr, OUString());
    }

    for ( int i = 1; i < css::ui::UIElementType::COUNT; i++ )
    {
        OUString aResourceType;
        if ( i == css::ui::UIElementType::MENUBAR )
            aResourceType = RESOURCETYPE_MENUBAR;
        else if ( i == css::ui::UIElementType::TOOLBAR )
            aResourceType = RESOURCETYPE_TOOLBAR;
        else if ( i == css::ui::UIElementType::STATUSBAR )
            aResourceType = RESOURCETYPE_STATUSBAR;
        else if ( i == css::ui::UIElementType::POPUPMENU )
            aResourceType = RESOURCETYPE_POPUPMENU;

        if ( !aResourceType.isEmpty() )
        {
            m_pStorageHandler[i].reset( new PresetHandler( m_xContext ) );
            m_pStorageHandler[i]->connectToResource( PresetHandler::E_MODULES,
                                                     aResourceType, // this path won't be used later... see next lines!
                                                     aModuleShortName,
                                                     css::uno::Reference< css::embed::XStorage >()); // no document root used here!
        }
    }

    // initialize root storages for all resource types
    m_xUserRootCommit.set( m_pStorageHandler[css::ui::UIElementType::MENUBAR]->getOrCreateRootStorageUser(), css::uno::UNO_QUERY); // can be empty
    m_xDefaultConfigStorage = m_pStorageHandler[css::ui::UIElementType::MENUBAR]->getParentStorageShare();
    m_xUserConfigStorage    = m_pStorageHandler[css::ui::UIElementType::MENUBAR]->getParentStorageUser();

    if ( m_xUserConfigStorage.is() )
    {
        Reference< XPropertySet > xPropSet( m_xUserConfigStorage, UNO_QUERY );
        if ( xPropSet.is() )
        {
            tools::Long nOpenMode = 0;
            Any a = xPropSet->getPropertyValue(u"OpenMode"_ustr);
            if ( a >>= nOpenMode )
                m_bReadOnly = !( nOpenMode & ElementModes::WRITE );
        }
    }

    impl_Initialize();
}

// XComponent
void SAL_CALL ModuleUIConfigurationManager::dispose()
{
    Reference< XComponent > xThis(this);

    css::lang::EventObject aEvent( xThis );
    {
        std::unique_lock aGuard(m_mutex);
        m_aEventListeners.disposeAndClear( aGuard, aEvent );
    }
    {
        std::unique_lock aGuard(m_mutex);
        m_aConfigListeners.disposeAndClear( aGuard, aEvent );
    }

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    SolarMutexClearableGuard aGuard;
    rtl::Reference< ImageManager > xModuleImageManager = std::move( m_xModuleImageManager );
    m_xModuleAcceleratorManager.clear();
    m_aUIElements[LAYER_USERDEFINED].clear();
    m_aUIElements[LAYER_DEFAULT].clear();
    m_xDefaultConfigStorage.clear();
    m_xUserConfigStorage.clear();
    m_xUserRootCommit.clear();
    m_bModified = false;
    m_bDisposed = true;
    aGuard.clear();
    /* SAFE AREA ----------------------------------------------------------------------------------------------- */

    try
    {
        if ( xModuleImageManager.is() )
            xModuleImageManager->dispose();
    }
    catch ( const Exception& )
    {
    }
}

void SAL_CALL ModuleUIConfigurationManager::addEventListener( const Reference< XEventListener >& xListener )
{
    {
        SolarMutexGuard g;

        /* SAFE AREA ----------------------------------------------------------------------------------------------- */
        if ( m_bDisposed )
            throw DisposedException();
    }

    std::unique_lock aGuard(m_mutex);
    m_aEventListeners.addInterface( aGuard, xListener );
}

void SAL_CALL ModuleUIConfigurationManager::removeEventListener( const Reference< XEventListener >& xListener )
{
    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    std::unique_lock aGuard(m_mutex);
    m_aEventListeners.removeInterface( aGuard, xListener );
}

// XUIConfiguration
void SAL_CALL ModuleUIConfigurationManager::addConfigurationListener( const Reference< css::ui::XUIConfigurationListener >& xListener )
{
    {
        SolarMutexGuard g;

        /* SAFE AREA ----------------------------------------------------------------------------------------------- */
        if ( m_bDisposed )
            throw DisposedException();
    }

    std::unique_lock aGuard(m_mutex);
    m_aConfigListeners.addInterface( aGuard, xListener );
}

void SAL_CALL ModuleUIConfigurationManager::removeConfigurationListener( const Reference< css::ui::XUIConfigurationListener >& xListener )
{
    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    std::unique_lock aGuard(m_mutex);
    m_aConfigListeners.removeInterface( aGuard, xListener );
}

// XUIConfigurationManager
void SAL_CALL ModuleUIConfigurationManager::reset()
{
    SolarMutexClearableGuard aGuard;

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    if ( m_bDisposed )
        throw DisposedException();

    if ( isReadOnly() )
        return;

    // Remove all elements from our user-defined storage!
    try
    {
        for ( int i = 1; i < css::ui::UIElementType::COUNT; i++ )
        {
            UIElementType&        rElementType = m_aUIElements[LAYER_USERDEFINED][i];

            if ( rElementType.xStorage.is() )
            {
                bool bCommitSubStorage( false );
                const Sequence< OUString > aUIElementStreamNames = rElementType.xStorage->getElementNames();
                for ( OUString const & rName : aUIElementStreamNames )
                {
                    rElementType.xStorage->removeElement( rName );
                    bCommitSubStorage = true;
                }

                if ( bCommitSubStorage )
                {
                    Reference< XTransactedObject > xTransactedObject( rElementType.xStorage, UNO_QUERY );
                    if ( xTransactedObject.is() )
                        xTransactedObject->commit();
                    m_pStorageHandler[i]->commitUserChanges();
                }
            }
        }

        // remove settings from user defined layer and notify listener about removed settings data!
        ConfigEventNotifyContainer aRemoveEventNotifyContainer;
        ConfigEventNotifyContainer aReplaceEventNotifyContainer;
        for ( sal_Int16 j = 1; j < css::ui::UIElementType::COUNT; j++ )
        {
            try
            {
                UIElementType& rUserElementType     = m_aUIElements[LAYER_USERDEFINED][j];
                UIElementType& rDefaultElementType  = m_aUIElements[LAYER_DEFAULT][j];

                impl_resetElementTypeData( rUserElementType, rDefaultElementType, aRemoveEventNotifyContainer, aReplaceEventNotifyContainer );
                rUserElementType.bModified = false;
            }
            catch (const Exception&)
            {
                css::uno::Any anyEx = cppu::getCaughtException();
                throw css::lang::WrappedTargetRuntimeException(
                        u"ModuleUIConfigurationManager::reset exception"_ustr,
                        css::uno::Reference<css::uno::XInterface>(*this), anyEx);
            }
        }

        m_bModified = false;

        // Unlock mutex before notify our listeners
        aGuard.clear();

        // Notify our listeners
        for ( auto const & k: aRemoveEventNotifyContainer )
            implts_notifyContainerListener( k, NotifyOp_Remove );
        for ( auto const & k: aReplaceEventNotifyContainer )
            implts_notifyContainerListener( k, NotifyOp_Replace );
    }
    catch ( const css::lang::IllegalArgumentException& )
    {
    }
    catch ( const css::container::NoSuchElementException& )
    {
    }
    catch ( const css::embed::InvalidStorageException& )
    {
    }
    catch ( const css::embed::StorageWrappedTargetException& )
    {
    }
}

Sequence< Sequence< PropertyValue > > SAL_CALL ModuleUIConfigurationManager::getUIElementsInfo( sal_Int16 ElementType )
{
    if (( ElementType < 0 ) || ( ElementType >= css::ui::UIElementType::COUNT ))
        throw IllegalArgumentException();

    SolarMutexGuard g;
    if ( m_bDisposed )
        throw DisposedException();

    std::vector< Sequence< PropertyValue > > aElementInfoSeq;
    UIElementInfoHashMap aUIElementInfoCollection;

    if ( ElementType == css::ui::UIElementType::UNKNOWN )
    {
        for ( sal_Int16 i = 0; i < css::ui::UIElementType::COUNT; i++ )
            impl_fillSequenceWithElementTypeInfo( aUIElementInfoCollection, i );
    }
    else
        impl_fillSequenceWithElementTypeInfo( aUIElementInfoCollection, ElementType );

    aElementInfoSeq.resize( aUIElementInfoCollection.size() );

    sal_Int32 n = 0;
    for (auto const& elem : aUIElementInfoCollection)
    {
        aElementInfoSeq[n++] = Sequence<PropertyValue> {
            comphelper::makePropertyValue(u"ResourceURL"_ustr, elem.second.aResourceURL),
            comphelper::makePropertyValue(m_aPropUIName, elem.second.aUIName)
        };
    }

    return comphelper::containerToSequence(aElementInfoSeq);
}

Reference< XIndexContainer > SAL_CALL ModuleUIConfigurationManager::createSettings()
{
    SolarMutexGuard g;

    if ( m_bDisposed )
        throw DisposedException();

    // Creates an empty item container which can be filled from outside
    return Reference< XIndexContainer >( new RootItemContainer() );
}

sal_Bool SAL_CALL ModuleUIConfigurationManager::hasSettings( const OUString& ResourceURL )
{
    sal_Int16 nElementType = RetrieveTypeFromResourceURL( ResourceURL );

    if (( nElementType == css::ui::UIElementType::UNKNOWN ) ||
        ( nElementType >= css::ui::UIElementType::COUNT   ))
        throw IllegalArgumentException();

    SolarMutexGuard g;

    if ( m_bDisposed )
        throw DisposedException();

    UIElementData* pDataSettings = impl_findUIElementData( ResourceURL, nElementType, false );
    if ( pDataSettings )
        return true;

    return false;
}

Reference< XIndexAccess > SAL_CALL ModuleUIConfigurationManager::getSettings( const OUString& ResourceURL, sal_Bool bWriteable )
{
    sal_Int16 nElementType = RetrieveTypeFromResourceURL( ResourceURL );

    if (( nElementType == css::ui::UIElementType::UNKNOWN ) ||
        ( nElementType >= css::ui::UIElementType::COUNT   ))
        throw IllegalArgumentException();

    SolarMutexGuard g;

    if ( m_bDisposed )
        throw DisposedException();

    UIElementData* pDataSettings = impl_findUIElementData( ResourceURL, nElementType );
    if ( pDataSettings )
    {
        // Create a copy of our data if someone wants to change the data.
        if ( bWriteable )
            return Reference< XIndexAccess >( new RootItemContainer( pDataSettings->xSettings ) );
        else
            return pDataSettings->xSettings;
    }

    throw NoSuchElementException();
}

void SAL_CALL ModuleUIConfigurationManager::replaceSettings( const OUString& ResourceURL, const Reference< css::container::XIndexAccess >& aNewData )
{
    sal_Int16 nElementType = RetrieveTypeFromResourceURL( ResourceURL );

    if (( nElementType == css::ui::UIElementType::UNKNOWN ) ||
        ( nElementType >= css::ui::UIElementType::COUNT   ))
        throw IllegalArgumentException();
    else if ( m_bReadOnly )
        throw IllegalAccessException();
    else
    {
        SolarMutexClearableGuard aGuard;

        if ( m_bDisposed )
            throw DisposedException();

        UIElementData* pDataSettings = impl_findUIElementData( ResourceURL, nElementType );
        if ( !pDataSettings )
            throw NoSuchElementException();
        if ( !pDataSettings->bDefaultNode )
        {
            // we have a settings entry in our user-defined layer - replace
            Reference< XIndexAccess > xOldSettings = pDataSettings->xSettings;

            // Create a copy of the data if the container is not const
            Reference< XIndexReplace > xReplace( aNewData, UNO_QUERY );
            if ( xReplace.is() )
                pDataSettings->xSettings = new ConstItemContainer( aNewData );
            else
                pDataSettings->xSettings = aNewData;
            pDataSettings->bDefault  = false;
            pDataSettings->bModified = true;
            m_bModified = true;

            // Modify type container
            UIElementType& rElementType = m_aUIElements[LAYER_USERDEFINED][nElementType];
            rElementType.bModified = true;

            Reference< XUIConfigurationManager > xThis(this);

            // Create event to notify listener about replaced element settings
            ui::ConfigurationEvent aEvent;
            aEvent.ResourceURL = ResourceURL;
            aEvent.Accessor <<= xThis;
            aEvent.Source.set(xThis, UNO_QUERY);
            aEvent.ReplacedElement <<= xOldSettings;
            aEvent.Element <<= pDataSettings->xSettings;

            aGuard.clear();

            implts_notifyContainerListener( aEvent, NotifyOp_Replace );
        }
        else
        {
            // we have no settings in our user-defined layer - insert
            UIElementData aUIElementData;

            aUIElementData.bDefault     = false;
            aUIElementData.bDefaultNode = false;
            aUIElementData.bModified    = true;

            // Create a copy of the data if the container is not const
            Reference< XIndexReplace > xReplace( aNewData, UNO_QUERY );
            if ( xReplace.is() )
                aUIElementData.xSettings = new ConstItemContainer( aNewData );
            else
                aUIElementData.xSettings = aNewData;
            aUIElementData.aName        = RetrieveNameFromResourceURL( ResourceURL ) + m_aXMLPostfix;
            aUIElementData.aResourceURL = ResourceURL;
            m_bModified = true;

            // Modify type container
            UIElementType& rElementType = m_aUIElements[LAYER_USERDEFINED][nElementType];
            rElementType.bModified = true;

            UIElementDataHashMap& rElements = rElementType.aElementsHashMap;

            // Check our user element settings hash map as it can already contain settings that have been set to default!
            // If no node can be found, we have to insert it.
            UIElementDataHashMap::iterator pIter = rElements.find( ResourceURL );
            if ( pIter != rElements.end() )
                pIter->second = aUIElementData;
            else
                rElements.emplace( ResourceURL, aUIElementData );

            Reference< XUIConfigurationManager > xThis(this);

            // Create event to notify listener about replaced element settings
            ui::ConfigurationEvent aEvent;

            aEvent.ResourceURL = ResourceURL;
            aEvent.Accessor <<= xThis;
            aEvent.Source.set(xThis, UNO_QUERY);
            aEvent.ReplacedElement <<= pDataSettings->xSettings;
            aEvent.Element <<= aUIElementData.xSettings;

            aGuard.clear();

            implts_notifyContainerListener( aEvent, NotifyOp_Replace );
        }
    }
}

void SAL_CALL ModuleUIConfigurationManager::removeSettings( const OUString& ResourceURL )
{
    sal_Int16 nElementType = RetrieveTypeFromResourceURL( ResourceURL );

    if (( nElementType == css::ui::UIElementType::UNKNOWN ) ||
        ( nElementType >= css::ui::UIElementType::COUNT   ))
        throw IllegalArgumentException( "The ResourceURL is not valid or "
                                        "describes an unknown type. "
                                        "ResourceURL: " + ResourceURL, nullptr, 0 );
    else if ( m_bReadOnly )
        throw IllegalAccessException( "The configuration manager is read-only. "
                                      "ResourceURL: " + ResourceURL, nullptr );
    else
    {
        SolarMutexClearableGuard aGuard;

        if ( m_bDisposed )
            throw DisposedException( "The configuration manager has been disposed, "
                                     "and can't uphold its method specification anymore. "
                                     "ResourceURL: " + ResourceURL, nullptr );

        UIElementData* pDataSettings = impl_findUIElementData( ResourceURL, nElementType );
        if ( !pDataSettings )
            throw NoSuchElementException( "The settings data cannot be found. "
                                          "ResourceURL: " + ResourceURL, nullptr );
        // If element settings are default, we don't need to change anything!
        if ( pDataSettings->bDefault )
            return;
        else
        {
            Reference< XIndexAccess > xRemovedSettings = pDataSettings->xSettings;
            pDataSettings->bDefault = true;

            // check if this is a default layer node
            if ( !pDataSettings->bDefaultNode )
                pDataSettings->bModified = true; // we have to remove this node from the user layer!
            pDataSettings->xSettings.clear();
            m_bModified = true; // user layer must be written

            // Modify type container
            UIElementType& rElementType = m_aUIElements[LAYER_USERDEFINED][nElementType];
            rElementType.bModified = true;

            Reference< XUIConfigurationManager > xThis(this);
            Reference< XInterface > xIfac( xThis, UNO_QUERY );

            // Check if we have settings in the default layer which replaces the user-defined one!
            UIElementData* pDefaultDataSettings = impl_findUIElementData( ResourceURL, nElementType );
            if ( pDefaultDataSettings )
            {
                // Create event to notify listener about replaced element settings
                ui::ConfigurationEvent aEvent;

                aEvent.ResourceURL = ResourceURL;
                aEvent.Accessor <<= xThis;
                aEvent.Source = std::move(xIfac);
                aEvent.Element <<= xRemovedSettings;
                aEvent.ReplacedElement <<= pDefaultDataSettings->xSettings;

                aGuard.clear();

                implts_notifyContainerListener( aEvent, NotifyOp_Replace );
            }
            else
            {
                // Create event to notify listener about removed element settings
                ui::ConfigurationEvent aEvent;

                aEvent.ResourceURL = ResourceURL;
                aEvent.Accessor <<= xThis;
                aEvent.Source = std::move(xIfac);
                aEvent.Element <<= xRemovedSettings;

                aGuard.clear();

                implts_notifyContainerListener( aEvent, NotifyOp_Remove );
            }
        }
    }
}

void SAL_CALL ModuleUIConfigurationManager::insertSettings( const OUString& NewResourceURL, const Reference< XIndexAccess >& aNewData )
{
    sal_Int16 nElementType = RetrieveTypeFromResourceURL( NewResourceURL );

    if (( nElementType == css::ui::UIElementType::UNKNOWN ) ||
        ( nElementType >= css::ui::UIElementType::COUNT   ))
        throw IllegalArgumentException();
    else if ( m_bReadOnly )
        throw IllegalAccessException();
    else
    {
        SolarMutexClearableGuard aGuard;

        if ( m_bDisposed )
            throw DisposedException();

        UIElementData* pDataSettings = impl_findUIElementData( NewResourceURL, nElementType );
        if ( !(!pDataSettings) )
            throw ElementExistException();
        UIElementData aUIElementData;

        aUIElementData.bDefault     = false;
        aUIElementData.bDefaultNode = false;
        aUIElementData.bModified    = true;

        // Create a copy of the data if the container is not const
        Reference< XIndexReplace > xReplace( aNewData, UNO_QUERY );
        if ( xReplace.is() )
            aUIElementData.xSettings = new ConstItemContainer( aNewData );
        else
            aUIElementData.xSettings = aNewData;
        aUIElementData.aName        = RetrieveNameFromResourceURL( NewResourceURL ) + m_aXMLPostfix;
        aUIElementData.aResourceURL = NewResourceURL;
        m_bModified = true;

        UIElementType& rElementType = m_aUIElements[LAYER_USERDEFINED][nElementType];
        rElementType.bModified = true;

        UIElementDataHashMap& rElements = rElementType.aElementsHashMap;
        rElements.emplace( NewResourceURL, aUIElementData );

        Reference< XIndexAccess > xInsertSettings( aUIElementData.xSettings );
        Reference< XUIConfigurationManager > xThis(this);

        // Create event to notify listener about removed element settings
        ui::ConfigurationEvent aEvent;

        aEvent.ResourceURL = NewResourceURL;
        aEvent.Accessor <<= xThis;
        aEvent.Source = xThis;
        aEvent.Element <<= xInsertSettings;

        aGuard.clear();

        implts_notifyContainerListener( aEvent, NotifyOp_Insert );
    }
}

Reference< XInterface > SAL_CALL ModuleUIConfigurationManager::getImageManager()
{
    SolarMutexGuard g;

    if ( m_bDisposed )
        throw DisposedException();

    if ( !m_xModuleImageManager.is() )
    {
        m_xModuleImageManager = new ImageManager( m_xContext, /*bForModule*/true );

        uno::Sequence<uno::Any> aPropSeq(comphelper::InitAnyPropertySequence(
        {
            {"UserConfigStorage", uno::Any(m_xUserConfigStorage)},
            {"ModuleIdentifier", uno::Any(m_aModuleIdentifier)},
            {"UserRootCommit", uno::Any(m_xUserRootCommit)},
        }));
        m_xModuleImageManager->initialize( aPropSeq );
    }

    return Reference< XInterface >( static_cast<cppu::OWeakObject*>(m_xModuleImageManager.get()), UNO_QUERY );
}

Reference< ui::XAcceleratorConfiguration > SAL_CALL ModuleUIConfigurationManager::createShortCutManager()
{
    return ui::ModuleAcceleratorConfiguration::createWithModuleIdentifier(m_xContext, m_aModuleIdentifier);
}

Reference< ui::XAcceleratorConfiguration > SAL_CALL ModuleUIConfigurationManager::getShortCutManager()
{
    SolarMutexGuard g;

    if ( m_bDisposed )
        throw DisposedException();

    if ( !m_xModuleAcceleratorManager.is() ) try
    {
        m_xModuleAcceleratorManager = ui::ModuleAcceleratorConfiguration::
            createWithModuleIdentifier(m_xContext, m_aModuleIdentifier);
    }
    catch ( const css::uno::DeploymentException& )
    {
        SAL_WARN("fwk.uiconfiguration", "ModuleAcceleratorConfiguration"
                " not available. This should happen only on mobile platforms.");
    }

    return m_xModuleAcceleratorManager;
}

Reference< XInterface > SAL_CALL ModuleUIConfigurationManager::getEventsManager()
{
    return Reference< XInterface >();
}

// XModuleUIConfigurationManager
sal_Bool SAL_CALL ModuleUIConfigurationManager::isDefaultSettings( const OUString& ResourceURL )
{
    sal_Int16 nElementType = RetrieveTypeFromResourceURL( ResourceURL );

    if (( nElementType == css::ui::UIElementType::UNKNOWN ) ||
        ( nElementType >= css::ui::UIElementType::COUNT   ))
        throw IllegalArgumentException();

    SolarMutexGuard g;

    if ( m_bDisposed )
        throw DisposedException();

    UIElementData* pDataSettings = impl_findUIElementData( ResourceURL, nElementType, false );
    if ( pDataSettings && pDataSettings->bDefaultNode )
        return true;

    return false;
}

Reference< XIndexAccess > SAL_CALL ModuleUIConfigurationManager::getDefaultSettings( const OUString& ResourceURL )
{
    sal_Int16 nElementType = RetrieveTypeFromResourceURL( ResourceURL );

    if (( nElementType == css::ui::UIElementType::UNKNOWN ) ||
        ( nElementType >= css::ui::UIElementType::COUNT   ))
        throw IllegalArgumentException();

    SolarMutexGuard g;

    if ( m_bDisposed )
        throw DisposedException();

    // preload list of element types on demand
    impl_preloadUIElementTypeList( LAYER_DEFAULT, nElementType );

    // Look into our default vector/unordered_map combination
    UIElementDataHashMap& rDefaultHashMap = m_aUIElements[LAYER_DEFAULT][nElementType].aElementsHashMap;
    UIElementDataHashMap::iterator pIter = rDefaultHashMap.find( ResourceURL );
    if ( pIter != rDefaultHashMap.end() )
    {
        if ( !pIter->second.xSettings.is() )
            impl_requestUIElementData( nElementType, LAYER_DEFAULT, pIter->second );
        return pIter->second.xSettings;
    }

    // Nothing has been found!
    throw NoSuchElementException();
}

// XUIConfigurationPersistence
void SAL_CALL ModuleUIConfigurationManager::reload()
{
    SolarMutexClearableGuard aGuard;

    if ( m_bDisposed )
        throw DisposedException();

    if ( !m_xUserConfigStorage.is() || !m_bModified || m_bReadOnly )
        return;

    // Try to access our module sub folder
    ConfigEventNotifyContainer aRemoveNotifyContainer;
    ConfigEventNotifyContainer aReplaceNotifyContainer;
    for ( sal_Int16 i = 1; i < css::ui::UIElementType::COUNT; i++ )
    {
        try
        {
            UIElementType& rUserElementType    = m_aUIElements[LAYER_USERDEFINED][i];

            if ( rUserElementType.bModified )
            {
                UIElementType& rDefaultElementType = m_aUIElements[LAYER_DEFAULT][i];
                impl_reloadElementTypeData( rUserElementType, rDefaultElementType, aRemoveNotifyContainer, aReplaceNotifyContainer );
            }
        }
        catch ( const Exception& )
        {
            throw IOException();
        }
    }

    m_bModified = false;

    // Unlock mutex before notify our listeners
    aGuard.clear();

    // Notify our listeners
    for (const ui::ConfigurationEvent & j : aRemoveNotifyContainer)
        implts_notifyContainerListener( j, NotifyOp_Remove );
    for (const ui::ConfigurationEvent & k : aReplaceNotifyContainer)
        implts_notifyContainerListener( k, NotifyOp_Replace );
}

void SAL_CALL ModuleUIConfigurationManager::store()
{
    SolarMutexGuard g;

    if ( m_bDisposed )
        throw DisposedException();

    if ( !m_xUserConfigStorage.is() || !m_bModified || m_bReadOnly )
        return;

    // Try to access our module sub folder
    for ( int i = 1; i < css::ui::UIElementType::COUNT; i++ )
    {
        try
        {
            UIElementType&        rElementType = m_aUIElements[LAYER_USERDEFINED][i];

            if ( rElementType.bModified && rElementType.xStorage.is() )
            {
                impl_storeElementTypeData( rElementType.xStorage, rElementType );
                m_pStorageHandler[i]->commitUserChanges();
            }
        }
        catch ( const Exception& )
        {
            throw IOException();
        }
    }

    m_bModified = false;
}

void SAL_CALL ModuleUIConfigurationManager::storeToStorage( const Reference< XStorage >& Storage )
{
    SolarMutexGuard g;

    if ( m_bDisposed )
        throw DisposedException();

    if ( !m_xUserConfigStorage.is() || !m_bModified || m_bReadOnly )
        return;

    // Try to access our module sub folder
    for ( int i = 1; i < css::ui::UIElementType::COUNT; i++ )
    {
        try
        {
            Reference< XStorage > xElementTypeStorage( Storage->openStorageElement(
                                                          OUString(UIELEMENTTYPENAMES[i]), ElementModes::READWRITE ));
            UIElementType&        rElementType = m_aUIElements[LAYER_USERDEFINED][i];

            if ( rElementType.bModified && xElementTypeStorage.is() )
                impl_storeElementTypeData( xElementTypeStorage, rElementType, false ); // store data to storage, but don't reset modify flag!
        }
        catch ( const Exception& )
        {
            throw IOException();
        }
    }

    Reference< XTransactedObject > xTransactedObject( Storage, UNO_QUERY );
    if ( xTransactedObject.is() )
        xTransactedObject->commit();
}

sal_Bool SAL_CALL ModuleUIConfigurationManager::isModified()
{
    SolarMutexGuard g;

    return m_bModified;
}

sal_Bool SAL_CALL ModuleUIConfigurationManager::isReadOnly()
{
    SolarMutexGuard g;

    return m_bReadOnly;
}

void ModuleUIConfigurationManager::implts_notifyContainerListener( const ui::ConfigurationEvent& aEvent, NotifyOp eOp )
{
    std::unique_lock aGuard(m_mutex);
    using ListenerMethodType = void (SAL_CALL css::ui::XUIConfigurationListener::*)(const ui::ConfigurationEvent&);
    ListenerMethodType aListenerMethod {};
    switch ( eOp )
    {
        case NotifyOp_Replace:
            aListenerMethod = &css::ui::XUIConfigurationListener::elementReplaced;
            break;
        case NotifyOp_Insert:
            aListenerMethod = &css::ui::XUIConfigurationListener::elementInserted;
            break;
        case NotifyOp_Remove:
            aListenerMethod = &css::ui::XUIConfigurationListener::elementRemoved;
            break;
    }
    m_aConfigListeners.notifyEach(aGuard, aListenerMethod, aEvent);
}

}

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface *
com_sun_star_comp_framework_ModuleUIConfigurationManager_get_implementation(
    css::uno::XComponentContext *context,
    css::uno::Sequence<css::uno::Any> const &arguments)
{
    return cppu::acquire(new ModuleUIConfigurationManager(context, arguments));
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
