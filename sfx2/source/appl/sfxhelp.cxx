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

#include <config_folders.h>
#include <sfx2/sfxhelp.hxx>
#include <helpids.h>

#include <string_view>
#include <algorithm>
#include <cassert>
#include <cstddef>
#ifdef MACOSX
#include <premac.h>
#include <Foundation/NSString.h>
#include <CoreFoundation/CFURL.h>
#include <CoreServices/CoreServices.h>
#include <postmac.h>
#endif

#include <sal/log.hxx>
#include <com/sun/star/uno/Reference.h>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/UnknownModuleException.hpp>
#include <com/sun/star/frame/XFrame2.hpp>
#include <comphelper/processfactory.hxx>
#include <com/sun/star/awt/XWindow.hpp>
#include <com/sun/star/awt/XTopWindow.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/frame/FrameSearchFlag.hpp>
#include <toolkit/helper/vclunohelper.hxx>
#include <com/sun/star/frame/ModuleManager.hpp>
#include <unotools/configmgr.hxx>
#include <unotools/moduleoptions.hxx>
#include <tools/urlobj.hxx>
#include <ucbhelper/content.hxx>
#include <unotools/pathoptions.hxx>
#include <rtl/byteseq.hxx>
#include <rtl/ustring.hxx>
#include <o3tl/environment.hxx>
#include <o3tl/string_view.hxx>
#include <officecfg/Office/Common.hxx>
#include <osl/process.h>
#include <osl/file.hxx>
#include <unotools/tempfile.hxx>
#include <unotools/securityoptions.hxx>
#include <rtl/uri.hxx>
#include <vcl/commandinfoprovider.hxx>
#include <vcl/keycod.hxx>
#include <vcl/settings.hxx>
#include <vcl/locktoplevels.hxx>
#include <vcl/weld.hxx>
#include <openuriexternally.hxx>

#include <comphelper/lok.hxx>
#include <LibreOfficeKit/LibreOfficeKitEnums.h>
#include <sfx2/viewsh.hxx>

#include "newhelp.hxx"
#include <sfx2/flatpak.hxx>
#include <sfx2/sfxresid.hxx>
#include <helper.hxx>
#include <sfx2/strings.hrc>
#include <vcl/svapp.hxx>
#include <rtl/string.hxx>
#include <svtools/langtab.hxx>
#include <comphelper/diagnose_ex.hxx>

using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::frame;
using namespace ::com::sun::star::uno;

namespace {

class NoHelpErrorBox
{
private:
    std::unique_ptr<weld::MessageDialog> m_xErrBox;
public:
    DECL_STATIC_LINK(NoHelpErrorBox, HelpRequestHdl, weld::Widget&, bool);
public:
    explicit NoHelpErrorBox(weld::Widget* pParent)
        : m_xErrBox(Application::CreateMessageDialog(pParent, VclMessageType::Error, VclButtonsType::Ok,
                                                     SfxResId(RID_STR_HLPFILENOTEXIST)))
    {
        // Error message: "No help available"
        m_xErrBox->connect_help(LINK(nullptr, NoHelpErrorBox, HelpRequestHdl));
    }
    void run()
    {
        m_xErrBox->run();
    }
};

}

IMPL_STATIC_LINK_NOARG(NoHelpErrorBox, HelpRequestHdl, weld::Widget&, bool)
{
    // do nothing, because no help available
    return false;
}

static OUString const & HelpLocaleString();

namespace {

/// Root path of the help.
OUString const & getHelpRootURL()
{
    static OUString const s_instURL = []()
    {
        OUString tmp = officecfg::Office::Common::Path::Current::Help::get();
        if (tmp.isEmpty())
        {
            // try to determine path from default
            tmp = "$(instpath)/" LIBO_SHARE_HELP_FOLDER;
        }

        // replace anything like $(instpath);
        SvtPathOptions aOptions;
        tmp = aOptions.SubstituteVariable(tmp);

        OUString url;
        if (osl::FileBase::getFileURLFromSystemPath(tmp, url) == osl::FileBase::E_None)
            tmp = url;
        return tmp;
    }();
    return s_instURL;
}

bool impl_checkHelpLocalePath(OUString const & rpPath)
{
    osl::DirectoryItem directoryItem;
    bool bOK = false;

    osl::FileStatus fileStatus(osl_FileStatus_Mask_Type | osl_FileStatus_Mask_FileURL | osl_FileStatus_Mask_FileName);
    if (osl::DirectoryItem::get(rpPath, directoryItem) == osl::FileBase::E_None &&
        directoryItem.getFileStatus(fileStatus) == osl::FileBase::E_None &&
        fileStatus.isDirectory())
    {
        bOK = true;
    }
    return bOK;
}

/// Check for built-in help
/// Check if help/<lang>/err.html file exist
bool impl_hasHelpInstalled()
{
    if (comphelper::LibreOfficeKit::isActive())
        return false;

        // detect installed locale
    static OUString const aLocaleStr = HelpLocaleString();

    OUString helpRootURL = getHelpRootURL() + "/" + aLocaleStr + "/err.html";
    bool bOK = false;
    osl::DirectoryItem directoryItem;
    if(osl::DirectoryItem::get(helpRootURL, directoryItem) == osl::FileBase::E_None){
        bOK=true;
    }

    SAL_INFO( "sfx.appl", "Checking old help installed " << bOK);
    return bOK;
}

/// Check for html built-in help
/// Check if help/lang/text folder exist. Only html has it.
bool impl_hasHTMLHelpInstalled()
{
    if (comphelper::LibreOfficeKit::isActive())
        return false;

    // detect installed locale
    static OUString const aLocaleStr = HelpLocaleString();

    OUString helpRootURL = getHelpRootURL() + "/" + aLocaleStr + "/text";
    bool bOK = impl_checkHelpLocalePath( helpRootURL );
    SAL_INFO( "sfx.appl", "Checking new help (html) installed " << bOK);
    return bOK;
}

} // namespace

/// Return the locale we prefer for displaying help
static OUString const & HelpLocaleString()
{
    if (comphelper::LibreOfficeKit::isActive())
        return comphelper::LibreOfficeKit::getLanguageTag().getBcp47();

    static OUString aLocaleStr;
    if (!aLocaleStr.isEmpty())
        return aLocaleStr;

    static constexpr OUString aEnglish(u"en-US"_ustr);
    // detect installed locale
    aLocaleStr = utl::ConfigManager::getUILocale();

    if ( aLocaleStr.isEmpty() )
    {
        aLocaleStr = aEnglish;
        return aLocaleStr;
    }

    // get fall-back language (country)
    OUString sLang = aLocaleStr;
    sal_Int32 nSepPos = sLang.indexOf( '-' );
    if (nSepPos != -1)
    {
        sLang = sLang.copy( 0, nSepPos );
    }
    OUString sHelpPath(u""_ustr);
    sHelpPath = getHelpRootURL() + "/" + utl::ConfigManager::getProductVersion() + "/" + aLocaleStr;
    if (impl_checkHelpLocalePath(sHelpPath))
    {
        return aLocaleStr;
    }
    sHelpPath = getHelpRootURL() + "/" + utl::ConfigManager::getProductVersion() + "/" + sLang;
    if (impl_checkHelpLocalePath(sHelpPath))
    {
        aLocaleStr = sLang;
        return aLocaleStr;
    }
    sHelpPath = getHelpRootURL() + "/" + aLocaleStr;
    if (impl_checkHelpLocalePath(sHelpPath))
    {
        return aLocaleStr;
    }
    sHelpPath = getHelpRootURL() + "/" + sLang;
    if (impl_checkHelpLocalePath(sHelpPath))
    {
        aLocaleStr = sLang;
        return aLocaleStr;
    }
    sHelpPath = getHelpRootURL() + "/" + utl::ConfigManager::getProductVersion() + "/" + aEnglish;
    if (impl_checkHelpLocalePath(sHelpPath))
    {
        aLocaleStr = aEnglish;
        return aLocaleStr;
    }
    sHelpPath = getHelpRootURL() + "/" + aEnglish;
    if (impl_checkHelpLocalePath(sHelpPath))
    {
        aLocaleStr = aEnglish;
        return aLocaleStr;
    }
    return aLocaleStr;
}



void AppendConfigToken( OUStringBuffer& rURL, bool bQuestionMark )
{
    const OUString& aLocaleStr = HelpLocaleString();

    // query part exists?
    if ( bQuestionMark )
        // no, so start with '?'
        rURL.append('?');
    else
        // yes, so only append with '&'
        rURL.append('&');

    // set parameters
    rURL.append("Language=");
    rURL.append(aLocaleStr);
    rURL.append("&System=");
    rURL.append(officecfg::Office::Common::Help::System::get());
    rURL.append("&Version=");
    rURL.append(utl::ConfigManager::getProductVersion());
}

static bool GetHelpAnchor_Impl( std::u16string_view _rURL, OUString& _rAnchor )
{
    bool bRet = false;

    try
    {
        ::ucbhelper::Content aCnt( INetURLObject( _rURL ).GetMainURL( INetURLObject::DecodeMechanism::NONE ),
                             Reference< css::ucb::XCommandEnvironment >(),
                             comphelper::getProcessComponentContext() );
        OUString sAnchor;
        if ( aCnt.getPropertyValue(u"AnchorName"_ustr) >>= sAnchor )
        {

            if ( !sAnchor.isEmpty() )
            {
                _rAnchor = sAnchor;
                bRet = true;
            }
        }
        else
        {
            SAL_WARN( "sfx.appl", "Property 'AnchorName' is missing" );
        }
    }
    catch (const css::uno::Exception&)
    {
    }

    return bRet;
}

namespace {

class SfxHelp_Impl
{
public:
    static OUString GetHelpText( const OUString& aCommandURL, const OUString& rModule );
};

}

OUString SfxHelp_Impl::GetHelpText( const OUString& aCommandURL, const OUString& rModule )
{
    // create help url
    OUStringBuffer aHelpURL( SfxHelp::CreateHelpURL( aCommandURL, rModule ) );
    // added 'active' parameter
    sal_Int32 nIndex = aHelpURL.lastIndexOf( '#' );
    if ( nIndex < 0 )
        nIndex = aHelpURL.getLength();
    aHelpURL.insert( nIndex, "&Active=true" );
    // load help string
    return SfxContentHelper::GetActiveHelpString( aHelpURL.makeStringAndClear() );
}

SfxHelp::SfxHelp()
    : bIsDebug(false)
    , bLaunchingHelp(false)
{
    // read the environment variable "HELP_DEBUG"
    // if it's set, you will see debug output on active help
    bIsDebug = !o3tl::getEnvironment(u"HELP_DEBUG"_ustr).isEmpty();
}

SfxHelp::~SfxHelp()
{
}

static OUString getDefaultModule_Impl()
{
    OUString sDefaultModule;
    SvtModuleOptions aModOpt;
    if (aModOpt.IsWriterInstalled())
        sDefaultModule = "swriter";
    else if (aModOpt.IsCalcInstalled())
        sDefaultModule = "scalc";
    else if (aModOpt.IsImpressInstalled())
        sDefaultModule = "simpress";
    else if (aModOpt.IsDrawInstalled())
        sDefaultModule = "sdraw";
    else if (aModOpt.IsMathInstalled())
        sDefaultModule = "smath";
    else if (aModOpt.IsChartInstalled())
        sDefaultModule = "schart";
    else if (SvtModuleOptions::IsBasicIDEInstalled())
        sDefaultModule = "sbasic";
    else if (aModOpt.IsDataBaseInstalled())
        sDefaultModule = "sdatabase";
    else
    {
        SAL_WARN( "sfx.appl", "getDefaultModule_Impl(): no module installed" );
    }
    return sDefaultModule;
}

static OUString getCurrentModuleIdentifier_Impl()
{
    OUString sIdentifier;
    const Reference < XComponentContext >& xContext = ::comphelper::getProcessComponentContext();
    Reference < XModuleManager2 > xModuleManager = ModuleManager::create(xContext);
    Reference < XDesktop2 > xDesktop = Desktop::create(xContext);
    Reference < XFrame > xCurrentFrame = xDesktop->getCurrentFrame();

    if ( xCurrentFrame.is() )
    {
        try
        {
            sIdentifier = xModuleManager->identify( xCurrentFrame );
        }
        catch (const css::frame::UnknownModuleException&)
        {
            SAL_INFO( "sfx.appl", "SfxHelp::getCurrentModuleIdentifier_Impl(): unknown module (help in help?)" );
        }
        catch (const Exception&)
        {
            TOOLS_WARN_EXCEPTION( "sfx.appl", "SfxHelp::getCurrentModuleIdentifier_Impl(): exception of XModuleManager::identify()" );
        }
    }

    return sIdentifier;
}

namespace
{
    OUString MapModuleIdentifier(const OUString &rFactoryShortName)
    {
        OUString aFactoryShortName(rFactoryShortName);

        // Map some module identifiers to their "real" help module string.
        if ( aFactoryShortName == "chart2" )
            aFactoryShortName = "schart" ;
        else if ( aFactoryShortName == "BasicIDE" )
            aFactoryShortName = "sbasic";
        else if ( aFactoryShortName == "sweb"
                || aFactoryShortName == "sglobal"
                || aFactoryShortName == "swxform" )
            aFactoryShortName = "swriter" ;
        else if ( aFactoryShortName == "dbquery"
                || aFactoryShortName == "dbbrowser"
                || aFactoryShortName == "dbrelation"
                || aFactoryShortName == "dbtable"
                || aFactoryShortName == "dbapp"
                || aFactoryShortName == "dbreport"
                || aFactoryShortName == "dbtdata"
                || aFactoryShortName == "swreport"
                || aFactoryShortName == "swform" )
            aFactoryShortName = "sdatabase";
        else if ( aFactoryShortName == "sbibliography"
                || aFactoryShortName == "sabpilot"
                || aFactoryShortName == "scanner"
                || aFactoryShortName == "spropctrlr"
                || aFactoryShortName == "StartModule" )
            aFactoryShortName.clear();

        return aFactoryShortName;
    }
}

OUString SfxHelp::GetHelpModuleName_Impl(std::u16string_view rHelpID)
{
    OUString aFactoryShortName;

    //rhbz#1438876 detect preferred module for this help id, e.g. csv dialog
    //for calc import before any toplevel is created and so context is
    //otherwise unknown. Cosmetic, same help is shown in any case because its
    //in the shared section, but title bar would state "Writer" when context is
    //expected to be "Calc"
    std::u16string_view sRemainder;
    if (o3tl::starts_with(rHelpID, u"modules/", &sRemainder))
    {
        std::size_t nEndModule = sRemainder.find(u'/');
        aFactoryShortName = nEndModule != std::u16string_view::npos
            ? sRemainder.substr(0, nEndModule) : sRemainder;
    }

    if (aFactoryShortName.isEmpty())
    {
        OUString aModuleIdentifier = getCurrentModuleIdentifier_Impl();
        if (!aModuleIdentifier.isEmpty())
        {
            try
            {
                Reference < XModuleManager2 > xModuleManager(
                    ModuleManager::create(::comphelper::getProcessComponentContext()) );
                Sequence< PropertyValue > lProps;
                xModuleManager->getByName( aModuleIdentifier ) >>= lProps;
                auto pProp = std::find_if(std::cbegin(lProps), std::cend(lProps),
                    [](const PropertyValue& rProp) { return rProp.Name == "ooSetupFactoryShortName"; });
                if (pProp != std::cend(lProps))
                    pProp->Value >>= aFactoryShortName;
            }
            catch (const Exception&)
            {
                TOOLS_WARN_EXCEPTION( "sfx.appl", "SfxHelp::GetHelpModuleName_Impl()" );
            }
        }
    }

    if (!aFactoryShortName.isEmpty())
        aFactoryShortName = MapModuleIdentifier(aFactoryShortName);
    if (aFactoryShortName.isEmpty())
        aFactoryShortName = getDefaultModule_Impl();

    return aFactoryShortName;
}

OUString SfxHelp::CreateHelpURL_Impl( const OUString& aCommandURL, const OUString& rModuleName )
{
    // build up the help URL
    OUStringBuffer aHelpURL("vnd.sun.star.help://");
    bool bHasAnchor = false;
    OUString aAnchor;

    OUString aModuleName( rModuleName );
    if (aModuleName.isEmpty())
        aModuleName = getDefaultModule_Impl();

    aHelpURL.append(aModuleName);

    if ( aCommandURL.isEmpty() )
        aHelpURL.append("/start");
    else
    {
        aHelpURL.append("/" +
            rtl::Uri::encode(aCommandURL,
                              rtl_UriCharClassRelSegment,
                              rtl_UriEncodeKeepEscapes,
                              RTL_TEXTENCODING_UTF8));

        OUStringBuffer aTempURL = aHelpURL;
        AppendConfigToken( aTempURL, true );
        bHasAnchor = GetHelpAnchor_Impl(aTempURL, aAnchor);
    }

    AppendConfigToken( aHelpURL, true );

    if ( bHasAnchor )
        aHelpURL.append("#" + aAnchor);

    return aHelpURL.makeStringAndClear();
}

static SfxHelpWindow_Impl* impl_createHelp(Reference< XFrame2 >& rHelpTask   ,
                                    Reference< XFrame >& rHelpContent)
{
    Reference < XDesktop2 > xDesktop = Desktop::create( ::comphelper::getProcessComponentContext() );

    // otherwise - create new help task
    Reference< XFrame2 > xHelpTask(
        xDesktop->findFrame(  u"OFFICE_HELP_TASK"_ustr, FrameSearchFlag::TASKS | FrameSearchFlag::CREATE),
        UNO_QUERY);
    if (!xHelpTask.is())
        return nullptr;

    // create all internal windows and sub frames ...
    Reference< css::awt::XWindow >      xParentWindow = xHelpTask->getContainerWindow();
    VclPtr<vcl::Window>                 pParentWindow = VCLUnoHelper::GetWindow( xParentWindow );
    VclPtrInstance<SfxHelpWindow_Impl>  pHelpWindow( xHelpTask, pParentWindow );
    Reference< css::awt::XWindow >      xHelpWindow   = VCLUnoHelper::GetInterface( pHelpWindow );

    Reference< XFrame > xHelpContent;
    if (xHelpTask->setComponent( xHelpWindow, Reference< XController >() ))
    {
        // Customize UI ...
        xHelpTask->setName(u"OFFICE_HELP_TASK"_ustr);

        Reference< XPropertySet > xProps(xHelpTask, UNO_QUERY);
        if (xProps.is())
            xProps->setPropertyValue(
                u"Title"_ustr,
                Any(SfxResId(STR_HELP_WINDOW_TITLE)));

        pHelpWindow->setContainerWindow( xParentWindow );
        xParentWindow->setVisible(true);
        xHelpWindow->setVisible(true);

        // This sub frame is created internally (if we called new SfxHelpWindow_Impl() ...)
        // It should exist :-)
        xHelpContent = xHelpTask->findFrame(u"OFFICE_HELP"_ustr, FrameSearchFlag::CHILDREN);
    }

    if (!xHelpContent.is())
    {
        pHelpWindow.disposeAndClear();
        return nullptr;
    }

    xHelpContent->setName(u"OFFICE_HELP"_ustr);

    rHelpTask    = std::move(xHelpTask);
    rHelpContent = std::move(xHelpContent);
    return pHelpWindow;
}

OUString SfxHelp::GetHelpText(const OUString& aCommandURL)
{
    OUString sModuleName = GetHelpModuleName_Impl(aCommandURL);
    auto aProperties = vcl::CommandInfoProvider::GetCommandProperties(aCommandURL, getCurrentModuleIdentifier_Impl());
    OUString sRealCommand = vcl::CommandInfoProvider::GetRealCommandForCommand(aProperties);
    OUString sHelpText = SfxHelp_Impl::GetHelpText( sRealCommand.isEmpty() ? aCommandURL : sRealCommand, sModuleName );

    // add some debug information?
    if ( bIsDebug )
    {
        sHelpText += "\n-------------\n" +
            sModuleName + ": " + aCommandURL;
    }

    return sHelpText;
}

OUString SfxHelp::GetURLHelpText(std::u16string_view aURL)
{
    // hyperlinks are handled differently in Online
    if (comphelper::LibreOfficeKit::isActive())
        return OUString();

    bool bCtrlClickHlink = SvtSecurityOptions::IsOptionSet(SvtSecurityOptions::EOption::CtrlClickHyperlink);

    // "ctrl-click to follow link:" for not MacOS
    // "⌘-click to follow link:" for MacOs
    vcl::KeyCode aCode(KEY_SPACE);
    vcl::KeyCode aModifiedCode(KEY_SPACE, KEY_MOD1);
    OUString aModStr(aModifiedCode.GetName());
    aModStr = aModStr.replaceFirst(aCode.GetName(), "");
    aModStr = aModStr.replaceAll("+", "");
    OUString aHelpStr
        = bCtrlClickHlink ? SfxResId(STR_CTRLCLICKHYPERLINK) : SfxResId(STR_CLICKHYPERLINK);
    aHelpStr = aHelpStr.replaceFirst("%{key}", aModStr);
    aHelpStr = aHelpStr.replaceFirst("%{link}", aURL);
    return aHelpStr;
}

void SfxHelp::SearchKeyword( const OUString& rKeyword )
{
    Start_Impl(OUString(), static_cast<weld::Widget*>(nullptr), rKeyword);
}

bool SfxHelp::Start( const OUString& rURL, const vcl::Window* pWindow )
{
    if (bLaunchingHelp)
        return true;
    bLaunchingHelp = true;
    bool bRet = Start_Impl( rURL, pWindow );
    bLaunchingHelp = false;
    return bRet;
}

bool SfxHelp::Start(const OUString& rURL, weld::Widget* pWidget)
{
    if (bLaunchingHelp)
        return true;
    bLaunchingHelp = true;
    bool bRet = Start_Impl(rURL, pWidget, OUString());
    bLaunchingHelp = false;
    return bRet;
}

/// Redirect the vnd.sun.star.help:// urls to http://help.libreoffice.org
static bool impl_showOnlineHelp(const OUString& rURL, weld::Widget* pDialogParent)
{
    static constexpr OUString aInternal(u"vnd.sun.star.help://"_ustr);
    if ( rURL.getLength() <= aInternal.getLength() || !rURL.startsWith(aInternal) )
        return false;

    OUString aHelpLink = officecfg::Office::Common::Help::HelpRootURL::get();
    OUString aTarget = OUString::Concat("Target=") + rURL.subView(aInternal.getLength());
    aTarget = aTarget.replaceAll("%2F", "/").replaceAll("?", "&");
    aHelpLink += aTarget;

    if (comphelper::LibreOfficeKit::isActive())
    {
        if(SfxViewShell* pViewShell = SfxViewShell::Current())
        {
            pViewShell->libreOfficeKitViewCallback(LOK_CALLBACK_HYPERLINK_CLICKED,
                                                   aHelpLink.toUtf8());
            return true;
        }
        else if (GetpApp())
        {
            GetpApp()->libreOfficeKitViewCallback(LOK_CALLBACK_HYPERLINK_CLICKED,
                                                   aHelpLink.toUtf8());
            return true;
        }

        return false;
    }

    try
    {
#ifdef MACOSX
        LSOpenCFURLRef(CFURLCreateWithString(kCFAllocatorDefault,
                           CFStringCreateWithCString(kCFAllocatorDefault,
                               aHelpLink.toUtf8().getStr(),
                               kCFStringEncodingUTF8),
                           nullptr),
            nullptr);
        (void)pDialogParent;
#else
        sfx2::openUriExternally(aHelpLink, false, pDialogParent);
#endif
        return true;
    }
    catch (const Exception&)
    {
    }
    return false;
}

namespace {

bool rewriteFlatpakHelpRootUrl(OUString * helpRootUrl) {
    assert(helpRootUrl != nullptr);
    //TODO: this function for now assumes that the passed-in *helpRootUrl references
    // /app/libreoffice/help (which belongs to the org.libreoffice.LibreOffice.Help
    // extension); it replaces it with the corresponding file URL as seen outside the flatpak
    // sandbox:
    struct Failure: public std::exception {};
    try {
        static auto const url = [] {
            // From /.flatpak-info [Instance] section, read
            //   app-path=<path>
            //   app-extensions=...;org.libreoffice.LibreOffice.Help=<sha>;...
            // lines:
            osl::File ini(u"file:///.flatpak-info"_ustr);
            auto err = ini.open(osl_File_OpenFlag_Read);
            if (err != osl::FileBase::E_None) {
                SAL_WARN("sfx.appl", "LIBO_FLATPAK mode failure opening /.flatpak-info: " << err);
                throw Failure();
            }
            OUString path;
            OUString extensions;
            bool havePath = false;
            bool haveExtensions = false;
            for (bool instance = false; !(havePath && haveExtensions);) {
                rtl::ByteSequence bytes;
                err = ini.readLine(bytes);
                if (err != osl::FileBase::E_None) {
                    SAL_WARN(
                        "sfx.appl",
                        "LIBO_FLATPAK mode reading /.flatpak-info fails with " << err
                            << " before [Instance] app-path");
                    throw Failure();
                }
                std::string_view const line(
                    reinterpret_cast<char const *>(bytes.getConstArray()), bytes.getLength());
                if (instance) {
                    static constexpr auto keyPath = std::string_view("app-path=");
                    static constexpr auto keyExtensions = std::string_view("app-extensions=");
                    if (!havePath && line.length() >= keyPath.size()
                        && line.substr(0, keyPath.size()) == keyPath.data())
                    {
                        auto const value = line.substr(keyPath.size());
                        if (!rtl_convertStringToUString(
                                &path.pData, value.data(), value.length(),
                                osl_getThreadTextEncoding(),
                                (RTL_TEXTTOUNICODE_FLAGS_UNDEFINED_ERROR
                                 | RTL_TEXTTOUNICODE_FLAGS_MBUNDEFINED_ERROR
                                 | RTL_TEXTTOUNICODE_FLAGS_INVALID_ERROR)))
                        {
                            SAL_WARN(
                                "sfx.appl",
                                "LIBO_FLATPAK mode failure converting app-path \"" << value
                                    << "\" encoding");
                            throw Failure();
                        }
                        havePath = true;
                    } else if (!haveExtensions && line.length() >= keyExtensions.size()
                               && line.substr(0, keyExtensions.size()) == keyExtensions.data())
                    {
                        auto const value = line.substr(keyExtensions.size());
                        if (!rtl_convertStringToUString(
                                &extensions.pData, value.data(), value.length(),
                                osl_getThreadTextEncoding(),
                                (RTL_TEXTTOUNICODE_FLAGS_UNDEFINED_ERROR
                                 | RTL_TEXTTOUNICODE_FLAGS_MBUNDEFINED_ERROR
                                 | RTL_TEXTTOUNICODE_FLAGS_INVALID_ERROR)))
                        {
                            SAL_WARN(
                                "sfx.appl",
                                "LIBO_FLATPAK mode failure converting app-extensions \"" << value
                                    << "\" encoding");
                            throw Failure();
                        }
                        haveExtensions = true;
                    } else if (line.length() > 0 && line[0] == '[') {
                        SAL_WARN(
                            "sfx.appl",
                            "LIBO_FLATPAK mode /.flatpak-info lacks [Instance] app-path and"
                                " app-extensions");
                        throw Failure();
                    }
                } else if (line == "[Instance]") {
                    instance = true;
                }
            }
            ini.close();
            // Extract <sha> from ...;org.libreoffice.LibreOffice.Help=<sha>;...:
            std::u16string_view sha;
            for (sal_Int32 i = 0;;) {
                OUString elem = extensions.getToken(0, ';', i);
                if (elem.startsWith("org.libreoffice.LibreOffice.Help=", &sha)) {
                    break;
                }
                if (i == -1) {
                    SAL_WARN(
                        "sfx.appl",
                        "LIBO_FLATPAK mode /.flatpak-info [Instance] app-extensions \""
                            << extensions << "\" org.libreoffice.LibreOffice.Help");
                    throw Failure();
                }
            }
            // Assuming that <path> is of the form
            //   /.../app/org.libreoffice.LibreOffice/<arch>/<branch>/<sha'>/files
            // rewrite it as
            //   /.../runtime/org.libreoffice.LibreOffice.Help/<arch>/<branch>/<sha>/files
            // because the extension's files are stored at a different place than the app's files,
            // so use this hack until flatpak itself provides a better solution:
            static constexpr OUString segments = u"/app/org.libreoffice.LibreOffice/"_ustr;
            auto const i1 = path.lastIndexOf(segments);
                // use lastIndexOf instead of indexOf, in case the user-controlled prefix /.../
                // happens to contain such segments
            if (i1 == -1) {
                SAL_WARN(
                    "sfx.appl",
                    "LIBO_FLATPAK mode /.flatpak-info [Instance] app-path \"" << path
                        << "\" doesn't contain /app/org.libreoffice.LibreOffice/");
                throw Failure();
            }
            auto const i2 = i1 + segments.getLength();
            auto i3 = path.indexOf('/', i2);
            if (i3 == -1) {
                SAL_WARN(
                    "sfx.appl",
                    "LIBO_FLATPAK mode /.flatpak-info [Instance] app-path \"" << path
                        << "\" doesn't contain branch segment");
                throw Failure();
            }
            i3 = path.indexOf('/', i3 + 1);
            if (i3 == -1) {
                SAL_WARN(
                    "sfx.appl",
                    "LIBO_FLATPAK mode /.flatpak-info [Instance] app-path \"" << path
                        << "\" doesn't contain sha segment");
                throw Failure();
            }
            ++i3;
            auto const i4 = path.indexOf('/', i3);
            if (i4 == -1) {
                SAL_WARN(
                    "sfx.appl",
                    "LIBO_FLATPAK mode /.flatpak-info [Instance] app-path \"" << path
                        << "\" doesn't contain files segment");
                throw Failure();
            }
            path = path.subView(0, i1) + OUString::Concat("/runtime/org.libreoffice.LibreOffice.Help/")
                + path.subView(i2, i3 - i2) + sha + path.subView(i4);
            // Turn <path> into a file URL:
            OUString url_;
            err = osl::FileBase::getFileURLFromSystemPath(path, url_);
            if (err != osl::FileBase::E_None) {
                SAL_WARN(
                    "sfx.appl",
                    "LIBO_FLATPAK mode failure converting app-path \"" << path << "\" to URL: "
                        << err);
                throw Failure();
            }
            return url_;
        }();
        *helpRootUrl = url;
        return true;
    } catch (Failure &) {
        return false;
    }
}

}

// add <noscript> meta for browsers without javascript

constexpr OUStringLiteral SHTML1 = u"<!DOCTYPE HTML><html lang=\"en-US\"><head><meta charset=\"UTF-8\">";
constexpr OUStringLiteral SHTML2 = u"<noscript><meta http-equiv=\"refresh\" content=\"0; url='";
constexpr OUStringLiteral SHTML3 = u"/noscript.html'\"></noscript><meta http-equiv=\"refresh\" content=\"1; url='";
constexpr OUStringLiteral SHTML4 = u"'\"><script type=\"text/javascript\"> window.location.href = \"";
constexpr OUStringLiteral SHTML5 = u"\";</script><title>Help Page Redirection</title></head><body></body></html>";

// use a tempfile since e.g. xdg-open doesn't support URL-parameters with file:// URLs
static bool impl_showOfflineHelp(const OUString& rURL, weld::Widget* pDialogParent)
{
    OUString aBaseInstallPath = getHelpRootURL();
    // For the flatpak case, find the pathname outside the flatpak sandbox that corresponds to
    // aBaseInstallPath, because that is what needs to be stored in aTempFile below:
    if (flatpak::isFlatpak() && !rewriteFlatpakHelpRootUrl(&aBaseInstallPath)) {
        return false;
    }

    OUString aHelpLink( aBaseInstallPath + "/index.html?" );
    OUString aTarget = OUString::Concat("Target=") + rURL.subView(RTL_CONSTASCII_LENGTH("vnd.sun.star.help://"));
    aTarget = aTarget.replaceAll("%2F","/").replaceAll("?","&");
    aHelpLink += aTarget;

    // Get a html tempfile (for the flatpak case, create it in XDG_CACHE_HOME instead of /tmp for
    // technical reasons, so that it can be accessed by the browser running outside the sandbox):
    static constexpr OUStringLiteral aExtension(u".html");
    OUString * parent = nullptr;
    if (flatpak::isFlatpak() && !flatpak::createTemporaryHtmlDirectory(&parent)) {
        return false;
    }
    ::utl::TempFileNamed aTempFile(u"NewHelp", true, aExtension, parent, false );

    SvStream* pStream = aTempFile.GetStream(StreamMode::WRITE);
    pStream->SetStreamCharSet(RTL_TEXTENCODING_UTF8);

    OUString aTempStr = SHTML1 + SHTML2 +
        aBaseInstallPath + "/" + HelpLocaleString() + SHTML3 +
        aHelpLink + SHTML4 +
        aHelpLink + SHTML5;

    pStream->WriteUnicodeOrByteText(aTempStr);

    aTempFile.CloseStream();
    try
    {
#ifdef MACOSX
        LSOpenCFURLRef(CFURLCreateWithString(kCFAllocatorDefault,
                           CFStringCreateWithCString(kCFAllocatorDefault,
                               aTempFile.GetURL().toUtf8().getStr(),
                               kCFStringEncodingUTF8),
                           nullptr),
            nullptr);
        (void)pDialogParent;
#else
        sfx2::openUriExternally(aTempFile.GetURL(), false, pDialogParent);
#endif
        return true;
    }
    catch (const Exception&)
    {
    }
    aTempFile.EnableKillingFile();
    return false;
}

namespace
{
    // tdf#119579 skip floating windows as potential parent for missing help dialog
    const vcl::Window* GetBestParent(const vcl::Window* pWindow)
    {
        while (pWindow)
        {
            if (pWindow->IsSystemWindow() && pWindow->GetType() != WindowType::FLOATINGWINDOW)
                break;
            pWindow = pWindow->GetParent();
        }
        return pWindow;
    }
}

namespace {

class HelpManualMessage : public weld::MessageDialogController
{
private:
    std::unique_ptr<weld::LinkButton> m_xDownloadInfo;
    std::unique_ptr<weld::CheckButton> m_xHideOfflineHelpCB;

    DECL_LINK(DownloadClickHdl, weld::LinkButton&, bool);
public:
    HelpManualMessage(weld::Widget* pParent)
        : MessageDialogController(pParent, u"sfx/ui/helpmanual.ui"_ustr, u"onlinehelpmanual"_ustr, u"box"_ustr)
        , m_xDownloadInfo(m_xBuilder->weld_link_button(u"downloadinfo"_ustr))
        , m_xHideOfflineHelpCB(m_xBuilder->weld_check_button(u"hidedialog"_ustr))
    {
        LanguageType aLangType = Application::GetSettings().GetUILanguageTag().getLanguageType();
        OUString sLocaleString = SvtLanguageTable::GetLanguageString(aLangType);
        OUString sPrimText = get_primary_text();
        set_primary_text(sPrimText.replaceAll("$UILOCALE", sLocaleString));

        m_xDownloadInfo->connect_activate_link(LINK(this, HelpManualMessage, DownloadClickHdl));
    }

    bool GetOfflineHelpPopUp() const { return !m_xHideOfflineHelpCB->get_active(); }
};

IMPL_LINK(HelpManualMessage, DownloadClickHdl, weld::LinkButton&, /* rButton */, bool)
{
    m_xDialog->response(RET_YES);
    return true;
}

}

bool SfxHelp::Start_Impl(const OUString& rURL, const vcl::Window* pWindow)
{
    OUStringBuffer aHelpRootURL("vnd.sun.star.help://");
    AppendConfigToken(aHelpRootURL, true);
    SfxContentHelper::GetResultSet(aHelpRootURL.makeStringAndClear());

    /* rURL may be
     *       - a "real" URL
     *       - a HelpID (formerly a long, now a string)
     *      If rURL is a URL, CreateHelpURL should be called for this URL
     *      If rURL is an arbitrary string, the same should happen, but the URL should be tried out
     *      if it delivers real help content. In case only the Help Error Document is returned, the
     *      parent of the window for that help was called, is asked for its HelpID.
     *      For compatibility reasons this upward search is not implemented for "real" URLs.
     *      Help keyword search now is implemented as own method; in former versions it
     *      was done via Help::Start, but this implementation conflicted with the upward search.
     */
    OUString aHelpURL;
    INetURLObject aParser( rURL );
    INetProtocol nProtocol = aParser.GetProtocol();

    switch ( nProtocol )
    {
        case INetProtocol::VndSunStarHelp:
            // already a vnd.sun.star.help URL -> nothing to do
            aHelpURL = rURL;
            break;
        default:
        {
            OUString aHelpModuleName(GetHelpModuleName_Impl(rURL));
            OUString aRealCommand;

            if ( nProtocol == INetProtocol::Uno )
            {
                // Command can be just an alias to another command.
                auto aProperties = vcl::CommandInfoProvider::GetCommandProperties(rURL, getCurrentModuleIdentifier_Impl());
                aRealCommand = vcl::CommandInfoProvider::GetRealCommandForCommand(aProperties);
            }

            // no URL, just a HelpID (maybe empty in case of keyword search)
            aHelpURL = CreateHelpURL_Impl( aRealCommand.isEmpty() ? rURL : aRealCommand, aHelpModuleName );

            if ( impl_hasHelpInstalled() && pWindow && SfxContentHelper::IsHelpErrorDocument( aHelpURL ) )
            {
                // no help found -> try with parent help id.
                vcl::Window* pParent = pWindow->GetParent();
                while ( pParent )
                {
                    OUString aHelpId = pParent->GetHelpId();
                    aHelpURL = CreateHelpURL( aHelpId, aHelpModuleName );

                    if ( !SfxContentHelper::IsHelpErrorDocument( aHelpURL ) )
                    {
                        break;
                    }
                    else
                    {
                        pParent = pParent->GetParent();
                        if (!pParent)
                        {
                            // create help url of start page ( helpid == 0 -> start page)
                            aHelpURL = CreateHelpURL( OUString(), aHelpModuleName );
                        }
                    }
                }
            }
            break;
        }
    }

    pWindow = GetBestParent(pWindow);
    weld::Window* pWeldWindow = pWindow ? pWindow->GetFrameWeld() : nullptr;

    if ( comphelper::LibreOfficeKit::isActive() )
    {
        impl_showOnlineHelp(aHelpURL, pWeldWindow);
        return true;
    }
#ifdef MACOSX
    if (@available(macOS 10.14, *)) {
        // Workaround: Safari sandboxing prevents it from accessing files in the LibreOffice.app folder
        // force online-help instead if Safari is default browser.
        CFURLRef pBrowser = LSCopyDefaultApplicationURLForURL(
                                CFURLCreateWithString(
                                    kCFAllocatorDefault,
                                    static_cast<CFStringRef>(@"https://www.libreoffice.org"),
                                    nullptr),
                                kLSRolesAll, nullptr);
        if([static_cast<NSString*>(CFURLGetString(pBrowser)) hasSuffix:@"/Applications/Safari.app/"]) {
            impl_showOnlineHelp(aHelpURL, pWeldWindow);
            return true;
        }
    }
#endif

    // If the HTML or no help is installed, but aHelpURL nevertheless references valid help content,
    // that implies that this help content belongs to an extension (and thus would not be available
    // in neither the offline nor online HTML help); in that case, fall through to the "old-help to
    // display" code below:
    if (SfxContentHelper::IsHelpErrorDocument(aHelpURL))
    {
        if ( impl_hasHTMLHelpInstalled() && impl_showOfflineHelp(aHelpURL, pWeldWindow) )
        {
            return true;
        }

        if ( !impl_hasHelpInstalled() )
        {
            bool bShowOfflineHelpPopUp = officecfg::Office::Common::Help::BuiltInHelpNotInstalledPopUp::get();
            short retOnlineHelpBox = RET_CLOSE;

            TopLevelWindowLocker aBusy;

            if(bShowOfflineHelpPopUp)
            {
                aBusy.incBusy(pWeldWindow);
                HelpManualMessage aQueryBox(pWeldWindow);
                retOnlineHelpBox = aQueryBox.run();
                auto xChanges = comphelper::ConfigurationChanges::create();
                officecfg::Office::Common::Help::BuiltInHelpNotInstalledPopUp::set(aQueryBox.GetOfflineHelpPopUp(), xChanges);
                xChanges->commit();
                aBusy.decBusy();
            }
            // Checks whether the user clicked "Read Help Online" (RET_OK) or "Information on downloading offline help" (RET_YES)
            if(!bShowOfflineHelpPopUp || retOnlineHelpBox == RET_OK || retOnlineHelpBox == RET_YES)
            {
                bool bTopicExists;

                if (!bShowOfflineHelpPopUp || retOnlineHelpBox == RET_OK)
                {
                    bTopicExists = impl_showOnlineHelp(aHelpURL, pWeldWindow);
                }
                else
                {
                    // Opens the help page that explains how to install offline help
                    OUString aOfflineHelpURL(CreateHelpURL_Impl(HID_HELPMANUAL_OFFLINE, u"shared"_ustr));
                    impl_showOnlineHelp(aOfflineHelpURL, pWeldWindow);
                    bTopicExists = true;
                }

                if (!bTopicExists)
                {
                    aBusy.incBusy(pWeldWindow);
                    NoHelpErrorBox aErrBox(pWeldWindow);
                    aErrBox.run();
                    aBusy.decBusy();
                    return false;
                }
                else
                {
                    return true;
                }
            }
            else
            {
                return false;
            }
        }
    }

    // old-help to display
    Reference < XDesktop2 > xDesktop = Desktop::create( ::comphelper::getProcessComponentContext() );

    // check if help window is still open
    // If not, create a new one and return access directly to the internal sub frame showing the help content
    // search must be done here; search one desktop level could return an arbitrary frame
    Reference< XFrame2 > xHelp(
        xDesktop->findFrame( u"OFFICE_HELP_TASK"_ustr, FrameSearchFlag::CHILDREN),
                               UNO_QUERY);
    Reference< XFrame > xHelpContent = xDesktop->findFrame(
        u"OFFICE_HELP"_ustr,
        FrameSearchFlag::CHILDREN);

    SfxHelpWindow_Impl* pHelpWindow = nullptr;
    if (!xHelp.is())
        pHelpWindow = impl_createHelp(xHelp, xHelpContent);
    else
        pHelpWindow = static_cast<SfxHelpWindow_Impl*>(VCLUnoHelper::GetWindow(xHelp->getComponentWindow()));
    if (!xHelp.is() || !xHelpContent.is() || !pHelpWindow)
        return false;

    SAL_INFO("sfx.appl", "HelpId = " << aHelpURL);

    pHelpWindow->SetHelpURL( aHelpURL );
    pHelpWindow->loadHelpContent(aHelpURL);

    Reference < css::awt::XTopWindow > xTopWindow( xHelp->getContainerWindow(), UNO_QUERY );
    if ( xTopWindow.is() )
        xTopWindow->toFront();

    return true;
}

bool SfxHelp::Start_Impl(const OUString& rURL, weld::Widget* pWidget, const OUString& rKeyword)
{
    OUStringBuffer aHelpRootURL("vnd.sun.star.help://");
    AppendConfigToken(aHelpRootURL, true);
    SfxContentHelper::GetResultSet(aHelpRootURL.makeStringAndClear());

    /* rURL may be
     *       - a "real" URL
     *       - a HelpID (formerly a long, now a string)
     *      If rURL is a URL, CreateHelpURL should be called for this URL
     *      If rURL is an arbitrary string, the same should happen, but the URL should be tried out
     *      if it delivers real help content. In case only the Help Error Document is returned, the
     *      parent of the window for that help was called, is asked for its HelpID.
     *      For compatibility reasons this upward search is not implemented for "real" URLs.
     *      Help keyword search now is implemented as own method; in former versions it
     *      was done via Help::Start, but this implementation conflicted with the upward search.
     */
    OUString aHelpURL;
    INetURLObject aParser( rURL );
    INetProtocol nProtocol = aParser.GetProtocol();

    switch ( nProtocol )
    {
        case INetProtocol::VndSunStarHelp:
            // already a vnd.sun.star.help URL -> nothing to do
            aHelpURL = rURL;
            break;
        default:
        {
            OUString aHelpModuleName(GetHelpModuleName_Impl(rURL));
            OUString aRealCommand;

            if ( nProtocol == INetProtocol::Uno )
            {
                // Command can be just an alias to another command.
                auto aProperties = vcl::CommandInfoProvider::GetCommandProperties(rURL, getCurrentModuleIdentifier_Impl());
                aRealCommand = vcl::CommandInfoProvider::GetRealCommandForCommand(aProperties);
            }

            // no URL, just a HelpID (maybe empty in case of keyword search)
            aHelpURL = CreateHelpURL_Impl( aRealCommand.isEmpty() ? rURL : aRealCommand, aHelpModuleName );

            if ( impl_hasHelpInstalled() && pWidget && SfxContentHelper::IsHelpErrorDocument( aHelpURL ) )
            {
                bool bUseFinalFallback = true;
                // no help found -> try ids of parents.
                pWidget->help_hierarchy_foreach([&aHelpModuleName, &aHelpURL, &bUseFinalFallback](const OUString& rHelpId){
                    if (rHelpId.isEmpty())
                        return false;
                    aHelpURL = CreateHelpURL(rHelpId, aHelpModuleName);
                    bool bFinished = !SfxContentHelper::IsHelpErrorDocument(aHelpURL);
                    if (bFinished)
                        bUseFinalFallback = false;
                    return bFinished;
                });

                if (bUseFinalFallback)
                {
                    // create help url of start page ( helpid == 0 -> start page)
                    aHelpURL = CreateHelpURL( OUString(), aHelpModuleName );
                }
            }
            break;
        }
    }

    if ( comphelper::LibreOfficeKit::isActive() )
    {
        impl_showOnlineHelp(aHelpURL, pWidget);
        return true;
    }
#ifdef MACOSX
    if (@available(macOS 10.14, *)) {
        // Workaround: Safari sandboxing prevents it from accessing files in the LibreOffice.app folder
        // force online-help instead if Safari is default browser.
        CFURLRef pBrowser = LSCopyDefaultApplicationURLForURL(
                                CFURLCreateWithString(
                                    kCFAllocatorDefault,
                                    static_cast<CFStringRef>(@"https://www.libreoffice.org"),
                                    nullptr),
                                kLSRolesAll, nullptr);
        if([static_cast<NSString*>(CFURLGetString(pBrowser)) hasSuffix:@"/Applications/Safari.app/"]) {
            impl_showOnlineHelp(aHelpURL, pWidget);
            return true;
        }
    }
#endif

    // If the HTML or no help is installed, but aHelpURL nevertheless references valid help content,
    // that implies that help content belongs to an extension (and thus would not be available
    // in neither the offline nor online HTML help); in that case, fall through to the "old-help to
    // display" code below:
    if (SfxContentHelper::IsHelpErrorDocument(aHelpURL))
    {
        if ( impl_hasHTMLHelpInstalled() && impl_showOfflineHelp(aHelpURL, pWidget) )
        {
            return true;
        }

        if ( !impl_hasHelpInstalled() )
        {
            bool bShowOfflineHelpPopUp = officecfg::Office::Common::Help::BuiltInHelpNotInstalledPopUp::get();
            short retOnlineHelpBox = RET_CLOSE;

            TopLevelWindowLocker aBusy;

            if(bShowOfflineHelpPopUp)
            {
                aBusy.incBusy(pWidget);
                HelpManualMessage aQueryBox(pWidget);
                retOnlineHelpBox = aQueryBox.run();
                auto xChanges = comphelper::ConfigurationChanges::create();
                officecfg::Office::Common::Help::BuiltInHelpNotInstalledPopUp::set(aQueryBox.GetOfflineHelpPopUp(), xChanges);
                xChanges->commit();
                aBusy.decBusy();
            }
            // Checks whether the user clicked "Read Help Online" (RET_OK) or "Information on downloading offline help" (RET_YES)
            if(!bShowOfflineHelpPopUp || retOnlineHelpBox == RET_OK || retOnlineHelpBox == RET_YES)
            {
                bool bTopicExists;

                if (!bShowOfflineHelpPopUp || retOnlineHelpBox == RET_OK)
                {
                    bTopicExists = impl_showOnlineHelp(aHelpURL, pWidget);
                }
                else
                {
                    // Opens the help page that explains how to install offline help
                    OUString aOfflineHelpURL(CreateHelpURL_Impl(HID_HELPMANUAL_OFFLINE, u"shared"_ustr));
                    impl_showOnlineHelp(aOfflineHelpURL, pWidget);
                    bTopicExists = true;
                }

                if (!bTopicExists)
                {
                    aBusy.incBusy(pWidget);
                    NoHelpErrorBox aErrBox(pWidget);
                    aErrBox.run();
                    aBusy.decBusy();
                    return false;
                }
                else
                {
                    return true;
                }
            }
            else
            {
                return false;
            }
        }
    }

    // old-help to display
    Reference < XDesktop2 > xDesktop = Desktop::create( ::comphelper::getProcessComponentContext() );

    // check if help window is still open
    // If not, create a new one and return access directly to the internal sub frame showing the help content
    // search must be done here; search one desktop level could return an arbitrary frame
    Reference< XFrame2 > xHelp(
        xDesktop->findFrame( u"OFFICE_HELP_TASK"_ustr, FrameSearchFlag::CHILDREN),
                               UNO_QUERY);
    Reference< XFrame > xHelpContent = xDesktop->findFrame(
        u"OFFICE_HELP"_ustr,
        FrameSearchFlag::CHILDREN);

    SfxHelpWindow_Impl* pHelpWindow = nullptr;
    if (!xHelp.is())
        pHelpWindow = impl_createHelp(xHelp, xHelpContent);
    else
        pHelpWindow = static_cast<SfxHelpWindow_Impl*>(VCLUnoHelper::GetWindow(xHelp->getComponentWindow()));
    if (!xHelp.is() || !xHelpContent.is() || !pHelpWindow)
        return false;

    SAL_INFO("sfx.appl", "HelpId = " << aHelpURL);

    pHelpWindow->SetHelpURL( aHelpURL );
    pHelpWindow->loadHelpContent(aHelpURL);
    if (!rKeyword.isEmpty())
        pHelpWindow->OpenKeyword( rKeyword );

    Reference < css::awt::XTopWindow > xTopWindow( xHelp->getContainerWindow(), UNO_QUERY );
    if ( xTopWindow.is() )
        xTopWindow->toFront();

    return true;
}

OUString SfxHelp::CreateHelpURL(const OUString& aCommandURL, const OUString& rModuleName)
{
    SfxHelp* pHelp = static_cast< SfxHelp* >(Application::GetHelp());
    return pHelp ? SfxHelp::CreateHelpURL_Impl( aCommandURL, rModuleName ) : OUString();
}

OUString SfxHelp::GetDefaultHelpModule()
{
    return getDefaultModule_Impl();
}

OUString SfxHelp::GetCurrentModuleIdentifier()
{
    return getCurrentModuleIdentifier_Impl();
}

bool SfxHelp::IsHelpInstalled()
{
    return impl_hasHelpInstalled();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
