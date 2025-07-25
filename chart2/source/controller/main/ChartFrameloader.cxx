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

#include "ChartFrameloader.hxx"
#include <servicenames.hxx>
#include <MediaDescriptorHelper.hxx>
#include <ChartController.hxx>
#include <ChartModel.hxx>
#include <unotools/fcm.hxx>
#include <unotools/mediadescriptor.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <com/sun/star/frame/XLoadable.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <comphelper/diagnose_ex.hxx>

namespace chart
{

using namespace ::com::sun::star;

ChartFrameLoader::ChartFrameLoader(
        uno::Reference<uno::XComponentContext> const & xContext)
        : m_bCancelRequired( false )
{
    m_xCC = xContext;
    m_oCancelFinished.reset();
}

ChartFrameLoader::~ChartFrameLoader()
{
}

bool ChartFrameLoader::impl_checkCancel()
{
    if(m_bCancelRequired)
    {
        m_oCancelFinished.set();
        return true;
    }
    return false;
}

// lang::XServiceInfo

OUString SAL_CALL ChartFrameLoader::getImplementationName()
{
    return u"com.sun.star.comp.chart2.ChartFrameLoader"_ustr;
}

sal_Bool SAL_CALL ChartFrameLoader::supportsService( const OUString& rServiceName )
{
    return cppu::supportsService(this, rServiceName);
}

css::uno::Sequence< OUString > SAL_CALL ChartFrameLoader::getSupportedServiceNames()
{
    return { u"com.sun.star.frame.SynchronousFrameLoader"_ustr };
}

// frame::XFrameLoader

sal_Bool SAL_CALL ChartFrameLoader::load( const uno::Sequence< beans::PropertyValue >& rMediaDescriptor, const uno::Reference<frame::XFrame >& xFrame )
{
    //@todo ? need to add as terminate listener to desktop?

    uno::Reference< frame::XModel >         xModel;
    bool bHaveLoadedModel = false;

    utl::MediaDescriptor aMediaDescriptor(rMediaDescriptor);
    {
        utl::MediaDescriptor::const_iterator aIt( aMediaDescriptor.find( utl::MediaDescriptor::PROP_MODEL));
        if( aIt != aMediaDescriptor.end())
        {
            xModel.set( (*aIt).second.get< uno::Reference< frame::XModel > >());
            bHaveLoadedModel = true;
        }
    }

    //create and initialize the model
    if( ! xModel.is())
    {
        //@todo?? load mechanism to cancel during loading of document
        xModel = new ChartModel(m_xCC);

        if( impl_checkCancel() )
            return false;
    }

    //create the controller(+XWindow)
    rtl::Reference< ChartController > xController = new ChartController(m_xCC);

    if( impl_checkCancel() )
        return false;

    //connect frame, controller and model one to each other:
    if(xModel.is())
    {
        utl::ConnectFrameControllerModel(xFrame, xController, xModel);
    }

    // call initNew() or load() at XLoadable
    if(bHaveLoadedModel)
        return true;

    try
    {
        utl::MediaDescriptor::const_iterator aIt( aMediaDescriptor.find( utl::MediaDescriptor::PROP_URL));
        if( aIt != aMediaDescriptor.end())
        {
            OUString aURL( (*aIt).second.get< OUString >());
            if( aURL.startsWith( "private:factory/schart" ) )
            {
                // create new file
                uno::Reference< frame::XLoadable > xLoadable( xModel, uno::UNO_QUERY_THROW );
                xLoadable->initNew();
            }
            else
            {
                // use the URL as BaseURL, similar to what SfxBaseModel effectively does
                if (!aURL.isEmpty())
                {
                    aMediaDescriptor[utl::MediaDescriptor::PROP_DOCUMENTBASEURL] <<= aURL;
                }
                aMediaDescriptor.addInputStream();
                uno::Sequence< beans::PropertyValue > aCompleteMediaDescriptor;
                aMediaDescriptor >> aCompleteMediaDescriptor;
                apphelper::MediaDescriptorHelper aMDHelper( aCompleteMediaDescriptor );

                // load file
                // @todo: replace: aMediaDescriptorHelper.getReducedForModel()
                uno::Reference< frame::XLoadable > xLoadable( xModel, uno::UNO_QUERY_THROW );
                xLoadable->load( aCompleteMediaDescriptor );

                //resize standalone files to get correct size:
                if( aMDHelper.ISSET_FilterName && aMDHelper.FilterName == "StarChart 5.0" )
                {
                    uno::Reference<awt::XWindow> xComponentWindow = xController->getComponentWindow();
                    awt::Rectangle aRect( xComponentWindow->getPosSize() );
                    xComponentWindow->setPosSize( aRect.X, aRect.Y, aRect.Width, aRect.Height, 0 );
                }
            }
        }
    }
    catch( const uno::Exception & )
    {
        DBG_UNHANDLED_EXCEPTION("chart2");
    }

    return true;
}

void SAL_CALL ChartFrameLoader::cancel()
{
    m_oCancelFinished.reset();
    m_bCancelRequired = true;
    m_oCancelFinished.wait();
    m_bCancelRequired = false;
}

} //namespace chart

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface *
com_sun_star_comp_chart2_ChartFrameLoader_get_implementation(css::uno::XComponentContext *context,
                                                             css::uno::Sequence<css::uno::Any> const &)
{
    return cppu::acquire(new chart::ChartFrameLoader(context));
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
