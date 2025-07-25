/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <sal/config.h>

#include <config_features.h>

#include <string_view>

#include <vcl/skia/SkiaHelper.hxx>

#if !HAVE_FEATURE_SKIA

namespace SkiaHelper
{
bool isVCLSkiaEnabled() { return false; }
bool isAlphaMaskBlendingEnabled() { return false; }

} // namespace

#else

#include <rtl/bootstrap.hxx>
#include <vcl/svapp.hxx>
#include <desktop/crashreport.hxx>
#include <officecfg/Office/Common.hxx>
#include <watchdog.hxx>
#include <skia/zone.hxx>
#include <sal/log.hxx>
#include <driverblocklist.hxx>
#include <skia/utils.hxx>
#include <config_folders.h>
#include <config_skia.h>
#include <osl/file.hxx>
#include <tools/stream.hxx>
#include <atomic>
#include <list>
#include <o3tl/lru_map.hxx>

#include <com/sun/star/configuration/theDefaultProvider.hpp>
#include <com/sun/star/util/XFlushable.hpp>

#include <SkBitmap.h>
#include <SkCanvas.h>
#include <include/codec/SkEncodedImageFormat.h>
#include <SkPaint.h>
#include <SkSurface.h>
#include <SkGraphics.h>
#include <ganesh/GrDirectContext.h>
#include <SkRuntimeEffect.h>
#include <SkStream.h>
#include <SkTileMode.h>
#include <skia_compiler.hxx>
#include <skia_opts.hxx>
#if defined(MACOSX)
#include <premac.h>
#endif
#ifdef SK_VULKAN
#include <tools/window/VulkanWindowContext.h>
#endif
#ifdef SK_METAL
#include <tools/window/MetalWindowContext.h>
#endif
#if defined(MACOSX)
#include <postmac.h>
#endif
#include <src/core/SkOpts.h>
#include <src/core/SkChecksum.h>
#include <include/encode/SkPngEncoder.h>
#include <ganesh/SkSurfaceGanesh.h>
#if defined _MSC_VER
#pragma warning(disable : 4100) // "unreferenced formal parameter"
#pragma warning(disable : 4324) // "structure was padded due to alignment specifier"
#endif
#if defined __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif
#if defined __GNUC__ && !defined __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <src/image/SkImage_Base.h>
#if defined __GNUC__ && !defined __clang__
#pragma GCC diagnostic pop
#endif
#if defined __clang__
#pragma clang diagnostic pop
#endif

#include <fstream>

#ifdef SK_METAL
#ifdef MACOSX
#include <quartz/cgutils.h>
#endif
#endif

namespace SkiaHelper
{
static OUString getCacheFolder()
{
    OUString url(u"${$BRAND_BASE_DIR/" LIBO_ETC_FOLDER
                 "/" SAL_CONFIGFILE("bootstrap") ":UserInstallation}/cache/"_ustr);
    rtl::Bootstrap::expandMacros(url);
    osl::Directory::create(url);
    return url;
}

static void writeToLog(SvStream& stream, const char* key, const char* value)
{
    stream.WriteOString(key);
    stream.WriteOString(": ");
    stream.WriteOString(value);
    stream.WriteChar('\n');
}

OUString readLog()
{
    SvFileStream logFile(getCacheFolder() + "/skia.log", StreamMode::READ);

    OUString sResult;
    OStringBuffer sLine;
    while (logFile.ReadLine(sLine))
        sResult += OStringToOUString(sLine, RTL_TEXTENCODING_UTF8) + "\n";

    return sResult;
}

uint32_t vendorId = 0;

#ifdef SK_VULKAN
static void writeToLog(SvStream& stream, const char* key, std::u16string_view value)
{
    writeToLog(stream, key, OUStringToOString(value, RTL_TEXTENCODING_UTF8).getStr());
}

static OUString getDenylistFile()
{
    OUString url(u"$BRAND_BASE_DIR/" LIBO_SHARE_FOLDER ""_ustr);
    rtl::Bootstrap::expandMacros(url);

    return url + "/skia/skia_denylist_vulkan.xml";
}

static OUString versionAsString(uint32_t version)
{
    return OUString::number(version >> 22) + "." + OUString::number((version >> 12) & 0x3ff) + "."
           + OUString::number(version & 0xfff);
}

static std::string_view vendorAsString(uint32_t vendor)
{
    return DriverBlocklist::GetVendorNameFromId(vendor);
}

// returns old value
static bool setForceSkiaRaster(bool val)
{
    const bool oldValue = officecfg::Office::Common::VCL::ForceSkiaRaster::get();
    if (oldValue != val && !officecfg::Office::Common::VCL::ForceSkiaRaster::isReadOnly())
    {
        auto batch(comphelper::ConfigurationChanges::create());
        officecfg::Office::Common::VCL::ForceSkiaRaster::set(val, batch);
        batch->commit();

        // make sure the change is written to the configuration
        if (auto xFlushable{ css::configuration::theDefaultProvider::get(
                                 comphelper::getProcessComponentContext())
                                 .query<css::util::XFlushable>() })
            xFlushable->flush();
    }
    return oldValue;
}

// Note that this function also logs system information about Vulkan.
static bool isVulkanDenylisted(const VkPhysicalDeviceProperties& props)
{
    static const char* const types[]
        = { "other", "integrated", "discrete", "virtual", "cpu", "??" }; // VkPhysicalDeviceType
    vendorId = props.vendorID;
    OUString vendorIdStr = "0x" + OUString::number(props.vendorID, 16);
    OUString deviceIdStr = "0x" + OUString::number(props.deviceID, 16);
    OUString driverVersionString = versionAsString(props.driverVersion);
    OUString apiVersion = versionAsString(props.apiVersion);
    const char* deviceType = types[std::min<unsigned>(props.deviceType, SAL_N_ELEMENTS(types) - 1)];

    CrashReporter::addKeyValue(u"VulkanVendor"_ustr, vendorIdStr, CrashReporter::AddItem);
    CrashReporter::addKeyValue(u"VulkanDevice"_ustr, deviceIdStr, CrashReporter::AddItem);
    CrashReporter::addKeyValue(u"VulkanAPI"_ustr, apiVersion, CrashReporter::AddItem);
    CrashReporter::addKeyValue(u"VulkanDriver"_ustr, driverVersionString, CrashReporter::AddItem);
    CrashReporter::addKeyValue(u"VulkanDeviceType"_ustr, OUString::createFromAscii(deviceType),
                               CrashReporter::AddItem);
    CrashReporter::addKeyValue(u"VulkanDeviceName"_ustr,
                               OUString::createFromAscii(props.deviceName), CrashReporter::Write);

    SvFileStream logFile(getCacheFolder() + "/skia.log", StreamMode::WRITE | StreamMode::TRUNC);
    writeToLog(logFile, "RenderMethod", "vulkan");
    writeToLog(logFile, "Vendor", vendorIdStr);
    writeToLog(logFile, "Device", deviceIdStr);
    writeToLog(logFile, "API", apiVersion);
    writeToLog(logFile, "Driver", driverVersionString);
    writeToLog(logFile, "DeviceType", deviceType);
    writeToLog(logFile, "DeviceName", props.deviceName);

    SAL_INFO("vcl.skia",
             "Vulkan API version: " << apiVersion << ", driver version: " << driverVersionString
                                    << ", vendor: " << vendorIdStr << " ("
                                    << vendorAsString(vendorId) << "), device: " << deviceIdStr
                                    << ", type: " << deviceType << ", name: " << props.deviceName);
    bool denylisted
        = DriverBlocklist::IsDeviceBlocked(getDenylistFile(), DriverBlocklist::VersionType::Vulkan,
                                           driverVersionString, vendorIdStr, deviceIdStr);
    writeToLog(logFile, "Denylisted", denylisted ? "yes" : "no");
    return denylisted;
}
#endif

#ifdef SK_METAL
static void writeSkiaMetalInfo()
{
    SvFileStream logFile(getCacheFolder() + "/skia.log", StreamMode::WRITE | StreamMode::TRUNC);
    writeToLog(logFile, "RenderMethod", "metal");
}
#endif

static void writeSkiaRasterInfo()
{
    SvFileStream logFile(getCacheFolder() + "/skia.log", StreamMode::WRITE | StreamMode::TRUNC);
    writeToLog(logFile, "RenderMethod", "raster");
    // Log compiler, Skia works best when compiled with Clang.
    writeToLog(logFile, "Compiler", skia_compiler_name());
}

#if defined(SK_VULKAN) || defined(SK_METAL)
static std::unique_ptr<skwindow::WindowContext> getTemporaryWindowContext();
#endif

static RenderMethod initRenderMethodToUse()
{
    if (Application::IsBitmapRendering())
        return RenderRaster;

    if (const char* env = getenv("SAL_SKIA"))
    {
        if (strcmp(env, "raster") == 0)
            return RenderRaster;
#if defined SK_METAL
        if (strcmp(env, "metal") == 0)
            return RenderMetal;
#elif defined SK_VULKAN
        if (strcmp(env, "vulkan") == 0)
            return RenderVulkan;
#endif
        SAL_WARN("vcl.skia", "Unrecognized value of SAL_SKIA");
        abort();
    }
    if (officecfg::Office::Common::VCL::ForceSkiaRaster::get())
        return RenderRaster;
#if defined SK_METAL
    return RenderMetal;
#elif defined SK_VULKAN
    return RenderVulkan;
#else
    return RenderRaster;
#endif
}

static std::atomic<RenderMethod>& accessRenderMethodToUse()
{
    static std::atomic<RenderMethod> methodToUse = initRenderMethodToUse();
    return methodToUse;
}

RenderMethod renderMethodToUse() { return accessRenderMethodToUse(); }

static void forceRasterRenderMethod() { accessRenderMethodToUse() = RenderRaster; }

static void checkDeviceDenylisted(bool blockDisable)
{
    static bool done = false;
    if (done)
        return;

    SkiaZone zone;

    bool useRaster = false;
    switch (renderMethodToUse())
    {
        case RenderVulkan:
        {
#ifdef SK_VULKAN
            // Temporarily change config to force software rendering. If the following HW check
            // crashes, this config change will stay active, and will make sure to avoid use of
            // faulty HW/driver on the nest start
            const bool oldForceSkiaRasterValue = setForceSkiaRaster(true);

            // First try if a GrDirectContext already exists.
            std::unique_ptr<skwindow::WindowContext> temporaryWindowContext;
            GrDirectContext* grDirectContext
                = skwindow::internal::VulkanWindowContext::getSharedGrDirectContext();
            if (!grDirectContext)
            {
                // This function is called from isVclSkiaEnabled(), which
                // may be called when deciding which X11 visual to use,
                // and that visual is normally needed when creating
                // Skia's VulkanWindowContext, which is needed for the GrDirectContext.
                // Avoid the loop by creating a temporary WindowContext
                // that will use the default X11 visual (that shouldn't matter
                // for just finding out information about Vulkan) and destroying
                // the temporary context will clean up again.
                temporaryWindowContext = getTemporaryWindowContext();
                grDirectContext
                    = skwindow::internal::VulkanWindowContext::getSharedGrDirectContext();
            }
            bool denylisted = true; // assume the worst
            if (grDirectContext) // Vulkan was initialized properly
            {
                denylisted = isVulkanDenylisted(
                    skwindow::internal::VulkanWindowContext::getPhysDeviceProperties());
                SAL_INFO("vcl.skia", "Vulkan denylisted: " << denylisted);
            }
            else
                SAL_INFO("vcl.skia", "Vulkan could not be initialized");
            if (denylisted && !blockDisable)
            {
                forceRasterRenderMethod();
                useRaster = true;
            }

            // The check succeeded; restore the original value
            setForceSkiaRaster(oldForceSkiaRasterValue);
#else
            SAL_WARN("vcl.skia", "Vulkan support not built in");
            (void)blockDisable;
            useRaster = true;
#endif
            break;
        }
        case RenderMetal:
        {
#ifdef SK_METAL
            // First try if a GrDirectContext already exists.
            std::unique_ptr<skwindow::WindowContext> temporaryWindowContext;
            GrDirectContext* grDirectContext = skwindow::internal::getMetalSharedGrDirectContext();
            if (!grDirectContext)
            {
                // Create a temporary window context just to get the GrDirectContext,
                // as an initial test of Metal functionality.
                temporaryWindowContext = getTemporaryWindowContext();
                grDirectContext = skwindow::internal::getMetalSharedGrDirectContext();
            }
            if (grDirectContext) // Metal was initialized properly
            {
#ifdef MACOSX
                if (!blockDisable && !DefaultMTLDeviceIsSupported())
                {
                    SAL_INFO("vcl.skia", "Metal default device not supported");
                    forceRasterRenderMethod();
                    useRaster = true;
                }
                else
#endif
                {
                    SAL_INFO("vcl.skia", "Using Skia Metal mode");
                    writeSkiaMetalInfo();
                }
            }
            else
            {
                SAL_INFO("vcl.skia", "Metal could not be initialized");
                forceRasterRenderMethod();
                useRaster = true;
            }
#else
            SAL_WARN("vcl.skia", "Metal support not built in");
            useRaster = true;
#endif
            break;
        }
        case RenderRaster:
            useRaster = true;
            break;
    }
    if (useRaster)
    {
        SAL_INFO("vcl.skia", "Using Skia raster mode");
        // software, never denylisted
        writeSkiaRasterInfo();
    }
    done = true;
}

static std::atomic<bool> skiaSupportedByBackend = false;
static bool supportsVCLSkia()
{
    if (skiaSupportedByBackend)
        return true;
    SAL_INFO("vcl.skia", "Skia not supported by VCL backend, disabling");
    return false;
}

static bool initVCLSkiaEnabled()
{
    /**
     * Should only be called once! Changing the results in the same
     * run will mix Skia and normal rendering.
     */

    // allow global disable when testing SystemPrimitiveRenderer since current Skia on Win does not
    // harmonize with using Direct2D and D2DPixelProcessor2D
    if (std::getenv("TEST_SYSTEM_PRIMITIVE_RENDERER") != nullptr)
        return false;

    /*
     * There are a number of cases that these environment variables cover:
     *  * SAL_FORCESKIA forces Skia if disabled by UI options or denylisted
     *  * SAL_DISABLESKIA avoids the use of Skia regardless of any option
     */

    bool bSalDisableSkia = getenv("SAL_DISABLESKIA") != nullptr;
#if defined(MACOSX) || defined(_WIN32)
    if (bSalDisableSkia)
    {
        SAL_WARN("vcl", "macOS/win requires Skia, so ignoring SAL_DISABLESKIA");
        bSalDisableSkia = false;
    }
#endif

    bool bRet = false;
    if (supportsVCLSkia() && !bSalDisableSkia)
    {
        const bool bForceSkia = getenv("SAL_FORCESKIA") != nullptr
                                || officecfg::Office::Common::VCL::ForceSkia::get();

        bRet = bForceSkia;
        // If not forced, don't enable in safe mode
        if (!bRet && !Application::IsSafeModeEnabled())
        {
#if defined(MACOSX) || defined(_WIN32)
            bRet = true; // macOS/win can __only__ render via skia
#else
            bRet = getenv("SAL_ENABLESKIA") != nullptr
                   || officecfg::Office::Common::VCL::UseSkia::get();
#endif
        }

        if (bRet)
        {
            // Set up all things needed for using Skia.
            SkGraphics::Init();
            SkLoOpts::Init();
            // if bForceSkia, don't actually block if denylisted, but log it if enabled,
            // and also get the vendor id; otherwise, switch to raster if driver is denylisted
            checkDeviceDenylisted(bForceSkia);
            WatchdogThread::start();
        }
    }

    CrashReporter::addKeyValue(u"UseSkia"_ustr, OUString::boolean(bRet), CrashReporter::Write);

    return bRet;
}

bool isVCLSkiaEnabled()
{
    static const bool val = initVCLSkiaEnabled();
    return val;
}

bool isAlphaMaskBlendingEnabled() { return false; }

// If needed, we'll allocate one extra window context so that we have a valid GrDirectContext
// from Vulkan/MetalWindowContext.
static std::unique_ptr<skwindow::WindowContext> sharedWindowContext;

static std::unique_ptr<skwindow::WindowContext> (*createGpuWindowContextFunction)(bool) = nullptr;
static void setCreateGpuWindowContext(std::unique_ptr<skwindow::WindowContext> (*function)(bool))
{
    createGpuWindowContextFunction = function;
}

GrDirectContext* getSharedGrDirectContext()
{
    SkiaZone zone;
    assert(renderMethodToUse() != RenderRaster);
    if (sharedWindowContext)
        return sharedWindowContext->directContext();
    // TODO mutex?
    // Set up the shared GrDirectContext from Skia's (patched) Vulkan/MetalWindowContext, if it's been
    // already set up.
    switch (renderMethodToUse())
    {
        case RenderVulkan:
#ifdef SK_VULKAN
            if (GrDirectContext* context
                = skwindow::internal::VulkanWindowContext::getSharedGrDirectContext())
                return context;
#endif
            break;
        case RenderMetal:
#ifdef SK_METAL
            if (GrDirectContext* context = skwindow::internal::getMetalSharedGrDirectContext())
                return context;
#endif
            break;
        case RenderRaster:
            abort();
    }
    static bool done = false;
    if (done)
        return nullptr;
    done = true;
    if (createGpuWindowContextFunction == nullptr)
        return nullptr; // not initialized properly (e.g. used from a VCL backend with no Skia support)
    sharedWindowContext = createGpuWindowContextFunction(false);
    GrDirectContext* grDirectContext
        = sharedWindowContext ? sharedWindowContext->directContext() : nullptr;
    if (grDirectContext)
        return grDirectContext;
    SAL_WARN_IF(renderMethodToUse() == RenderVulkan, "vcl.skia",
                "Cannot create Vulkan GPU context, falling back to Raster");
    SAL_WARN_IF(renderMethodToUse() == RenderMetal, "vcl.skia",
                "Cannot create Metal GPU context, falling back to Raster");
    forceRasterRenderMethod();
    return nullptr;
}

#if defined(SK_VULKAN) || defined(SK_METAL)
static std::unique_ptr<skwindow::WindowContext> getTemporaryWindowContext()
{
    if (createGpuWindowContextFunction == nullptr)
        return nullptr;
    return createGpuWindowContextFunction(true);
}
#endif

static RenderMethod renderMethodToUseForSize(const SkISize& size)
{
    // Do not use GPU for small surfaces. The problem is that due to the separate alpha hack
    // we quite often may call GetBitmap() on VirtualDevice, which is relatively slow
    // when the pixels need to be fetched from the GPU. And there are documents that use
    // many tiny surfaces (bsc#1183308 for example), where this slowness adds up too much.
    // This should be re-evaluated once the separate alpha hack is removed (SKIA_USE_BITMAP32)
    // and we no longer (hopefully) fetch pixels that often.
    if (size.width() <= 32 && size.height() <= 32)
        return RenderRaster;
    return renderMethodToUse();
}

sk_sp<SkSurface> createSkSurface(int width, int height, SkColorType type, SkAlphaType alpha)
{
    SkiaZone zone;
    assert(type == kN32_SkColorType || type == kAlpha_8_SkColorType);
    sk_sp<SkSurface> surface;
    switch (renderMethodToUseForSize({ width, height }))
    {
        case RenderVulkan:
        case RenderMetal:
        {
            if (GrDirectContext* grDirectContext = getSharedGrDirectContext())
            {
                surface = SkSurfaces::RenderTarget(grDirectContext, skgpu::Budgeted::kNo,
                                                   SkImageInfo::Make(width, height, type, alpha), 0,
                                                   surfaceProps());
                if (surface)
                {
#ifdef DBG_UTIL
                    prefillSurface(surface);
#endif
                    return surface;
                }
                SAL_WARN_IF(renderMethodToUse() == RenderVulkan, "vcl.skia",
                            "Cannot create Vulkan GPU offscreen surface, falling back to Raster");
                SAL_WARN_IF(renderMethodToUse() == RenderMetal, "vcl.skia",
                            "Cannot create Metal GPU offscreen surface, falling back to Raster");
            }
            break;
        }
        default:
            break;
    }
    // Create raster surface as a fallback.
    surface = SkSurfaces::Raster(SkImageInfo::Make(width, height, type, alpha), surfaceProps());
    assert(surface);
    if (surface)
    {
#ifdef DBG_UTIL
        prefillSurface(surface);
#endif
        return surface;
    }
    // In non-debug builds we could return SkSurface::MakeNull() and try to cope with the situation,
    // but that can lead to unnoticed data loss, so better fail clearly.
    abort();
}

sk_sp<SkImage> createSkImage(const SkBitmap& bitmap)
{
    SkiaZone zone;
    assert(bitmap.colorType() == kN32_SkColorType || bitmap.colorType() == kAlpha_8_SkColorType);
    switch (renderMethodToUseForSize(bitmap.dimensions()))
    {
        case RenderVulkan:
        case RenderMetal:
        {
            if (GrDirectContext* grDirectContext = getSharedGrDirectContext())
            {
                sk_sp<SkSurface> surface = SkSurfaces::RenderTarget(
                    grDirectContext, skgpu::Budgeted::kNo,
                    bitmap.info().makeAlphaType(kPremul_SkAlphaType), 0, surfaceProps());
                if (surface)
                {
                    SkPaint paint;
                    paint.setBlendMode(SkBlendMode::kSrc); // set as is, including alpha
                    surface->getCanvas()->drawImage(bitmap.asImage(), 0, 0, SkSamplingOptions(),
                                                    &paint);
                    return makeCheckedImageSnapshot(surface);
                }
                // Try to fall back in non-debug builds.
                SAL_WARN_IF(renderMethodToUse() == RenderVulkan, "vcl.skia",
                            "Cannot create Vulkan GPU offscreen surface, falling back to Raster");
                SAL_WARN_IF(renderMethodToUse() == RenderMetal, "vcl.skia",
                            "Cannot create Metal GPU offscreen surface, falling back to Raster");
            }
            break;
        }
        default:
            break;
    }
    // Create raster image as a fallback.
    sk_sp<SkImage> image = SkImages::RasterFromBitmap(bitmap);
    assert(image);
    return image;
}

sk_sp<SkImage> makeCheckedImageSnapshot(sk_sp<SkSurface> surface)
{
    sk_sp<SkImage> ret = surface->makeImageSnapshot();
    assert(ret);
    if (ret)
        return ret;
    abort();
}

sk_sp<SkImage> makeCheckedImageSnapshot(sk_sp<SkSurface> surface, const SkIRect& bounds)
{
    sk_sp<SkImage> ret = surface->makeImageSnapshot(bounds);
    assert(ret);
    if (ret)
        return ret;
    abort();
}

namespace
{
// Image cache, for saving results of complex operations such as drawTransformedBitmap().
struct ImageCacheItem
{
    OString key;
    sk_sp<SkImage> image;
    tools::Long size; // cost of the item
    long long keepInCacheUntilMilliseconds; // don't remove from cache before this timestamp
};
} //namespace

// LRU cache, last item is the least recently used. Hopefully there won't be that many items
// to require a hash/map. Using o3tl::lru_map would be simpler, but it doesn't support
// calculating cost of each item.
static std::list<ImageCacheItem> imageCache;
static tools::Long imageCacheSize = 0; // sum of all ImageCacheItem.size

// Related: tdf#166994 set the minimum amount of time that an image must
// remain in the image cache to 5 seconds. 2 seconds appears to be enough
// on macOS but set it to 5 seconds just to be safe.
const long long imageMinKeepInCacheMilliseconds = 5000;

void addCachedImage(const OString& key, sk_sp<SkImage> image)
{
    static bool disabled = getenv("SAL_DISABLE_SKIA_CACHE") != nullptr;
    if (disabled)
        return;
    tools::Long size = static_cast<tools::Long>(image->width()) * image->height()
                       * SkColorTypeBytesPerPixel(image->imageInfo().colorType());
    auto time = std::chrono::steady_clock::now().time_since_epoch();
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(time).count();
    imageCache.push_front({ key, image, size, now + imageMinKeepInCacheMilliseconds });
    imageCacheSize += size;
    SAL_INFO("vcl.skia.trace", "addcachedimage " << image << " :" << size << "/" << imageCacheSize);
    const tools::Long maxSize = maxImageCacheSize();
    while (imageCacheSize > maxSize)
    {
        assert(!imageCache.empty());

        // tdf#166994 Don't remove image from cache too quickly
        // While an animation is running, the same images are drawn in a
        // repeating cycle but each frame is removed from the cache before
        // it is drawn again.
        // Blending a bitmap with an alpha channel can be very slow so
        // it is much faster to keep all of the frames in the image cache.
        // So, allow the image cache to temporarily increase in size by
        // deferring removal of an image if it was cached within the last
        // few seconds.
        if (now < imageCache.back().keepInCacheUntilMilliseconds)
            break;

        imageCacheSize -= imageCache.back().size;
        SAL_INFO("vcl.skia.trace",
                 "least used removal " << imageCache.back().image << ":" << imageCache.back().size);
        imageCache.pop_back();
    }
}

sk_sp<SkImage> findCachedImage(const OString& key)
{
    for (auto it = imageCache.begin(); it != imageCache.end(); ++it)
    {
        if (it->key == key)
        {
            auto time = std::chrono::steady_clock::now().time_since_epoch();
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(time).count();
            it->keepInCacheUntilMilliseconds = now + imageMinKeepInCacheMilliseconds;
            sk_sp<SkImage> ret = it->image;
            SAL_INFO("vcl.skia.trace", "findcachedimage " << key << " : " << it->image << " found");
            imageCache.splice(imageCache.begin(), imageCache, it);
            return ret;
        }
    }
    SAL_INFO("vcl.skia.trace", "findcachedimage " << key << " not found");
    return nullptr;
}

void removeCachedImage(sk_sp<SkImage> image)
{
    for (auto it = imageCache.begin(); it != imageCache.end();)
    {
        if (it->image == image)
        {
            imageCacheSize -= it->size;
            assert(imageCacheSize >= 0);
            it = imageCache.erase(it);
        }
        else
            ++it;
    }
}

tools::Long maxImageCacheSize()
{
    // Defaults to 4x 2000px 32bpp images, 64MiB.
    return officecfg::Office::Common::Cache::Skia::ImageCacheSize::get();
}

static o3tl::lru_map<uint32_t, uint32_t> checksumCache(256);

static uint32_t computeSkPixmapChecksum(const SkPixmap& pixmap)
{
    // Use uint32_t because that's what SkChecksum::Hash32() returns.
    static_assert(std::is_same_v<uint32_t, decltype(SkChecksum::Hash32(nullptr, 0, 0))>);
    const size_t dataRowBytes = pixmap.width() << pixmap.shiftPerPixel();
    if (dataRowBytes == pixmap.rowBytes())
        return SkChecksum::Hash32(pixmap.addr(), pixmap.height() * dataRowBytes, 0);
    uint32_t sum = 0;
    for (int row = 0; row < pixmap.height(); ++row)
        sum = SkChecksum::Hash32(pixmap.addr(0, row), dataRowBytes, sum);
    return sum;
}

uint32_t getSkImageChecksum(sk_sp<SkImage> image)
{
    // Cache the checksums based on the uniqueID() (which should stay the same
    // for the same image), because it may be still somewhat expensive.
    uint32_t id = image->uniqueID();
    auto it = checksumCache.find(id);
    if (it != checksumCache.end())
        return it->second;
    SkPixmap pixmap;
    if (!image->peekPixels(&pixmap))
        abort(); // Fetching of GPU-based pixels is expensive, and shouldn't(?) be needed anyway.
    uint32_t checksum = computeSkPixmapChecksum(pixmap);
    checksumCache.insert({ id, checksum });
    return checksum;
}

static sk_sp<SkBlender> invertBlender;
static sk_sp<SkBlender> xorBlender;

// This does the invert operation, i.e. result = color(255-R,255-G,255-B,A).
void setBlenderInvert(SkPaint* paint)
{
    if (!invertBlender)
    {
        // Note that the colors are premultiplied, so '1 - dst.r' must be
        // written as 'dst.a - dst.r', since premultiplied R is in the range (0-A).
        const char* const diff = R"(
            vec4 main( vec4 src, vec4 dst )
            {
                return vec4( dst.a - dst.r, dst.a - dst.g, dst.a - dst.b, dst.a );
            }
        )";
        auto effect = SkRuntimeEffect::MakeForBlender(SkString(diff));
        if (!effect.effect)
        {
            SAL_WARN("vcl.skia",
                     "SKRuntimeEffect::MakeForBlender failed: " << effect.errorText.c_str());
            abort();
        }
        invertBlender = effect.effect->makeBlender(nullptr);
    }
    paint->setBlender(invertBlender);
}

// This does the xor operation, i.e. bitwise xor of RGB values of both colors.
void setBlenderXor(SkPaint* paint)
{
    if (!xorBlender)
    {
        // Note that the colors are premultiplied, converting to 0-255 range
        // must also unpremultiply.
        const char* const diff = R"(
            vec4 main( vec4 src, vec4 dst )
            {
                return vec4(
                    float(int(src.r * src.a * 255.0) ^ int(dst.r * dst.a * 255.0)) / 255.0 / dst.a,
                    float(int(src.g * src.a * 255.0) ^ int(dst.g * dst.a * 255.0)) / 255.0 / dst.a,
                    float(int(src.b * src.a * 255.0) ^ int(dst.b * dst.a * 255.0)) / 255.0 / dst.a,
                    dst.a );
            }
        )";
        SkRuntimeEffect::Options opts;
        // Skia does not allow binary operators in the default ES2Strict mode, but that's only
        // because of OpenGL support. We don't use OpenGL, and it's safe for all modes that we do use.
        // https://groups.google.com/g/skia-discuss/c/EPLuQbg64Kc/m/2uDXFIGhAwAJ
        opts.maxVersionAllowed = SkSL::Version::k300;
        auto effect = SkRuntimeEffect::MakeForBlender(SkString(diff), opts);
        if (!effect.effect)
        {
            SAL_WARN("vcl.skia",
                     "SKRuntimeEffect::MakeForBlender failed: " << effect.errorText.c_str());
            abort();
        }
        xorBlender = effect.effect->makeBlender(nullptr);
    }
    paint->setBlender(xorBlender);
}

void cleanup()
{
    sharedWindowContext.reset();
    imageCache.clear();
    imageCacheSize = 0;
    invertBlender.reset();
    xorBlender.reset();
}

static SkSurfaceProps commonSurfaceProps;
const SkSurfaceProps* surfaceProps() { return &commonSurfaceProps; }

void setPixelGeometry(SkPixelGeometry pixelGeometry)
{
    commonSurfaceProps = SkSurfaceProps(commonSurfaceProps.flags(), pixelGeometry);
}

// Skia should not be used from VCL backends that do not actually support it, as there will be setup missing.
// The code here (that is in the vcl lib) needs a function for creating Vulkan/Metal context that is
// usually available only in the backend libs.
void prepareSkia(std::unique_ptr<skwindow::WindowContext> (*createGpuWindowContext)(bool))
{
    setCreateGpuWindowContext(createGpuWindowContext);
    skiaSupportedByBackend = true;
}

void dump(const SkBitmap& bitmap, const char* file)
{
    dump(SkImages::RasterFromBitmap(bitmap), file);
}

void dump(const sk_sp<SkSurface>& surface, const char* file)
{
    if (auto dContext = GrAsDirectContext(surface->getCanvas()->recordingContext()))
        dContext->flushAndSubmit();
    dump(makeCheckedImageSnapshot(surface), file);
}

void dump(const sk_sp<SkImage>& image, const char* file)
{
    SkBitmap bm;
    if (!as_IB(image)->getROPixels(getSharedGrDirectContext(), &bm))
        return;
    SkPixmap pixmap;
    if (!bm.peekPixels(&pixmap))
        return;
    SkPngEncoder::Options opts;
    opts.fFilterFlags = SkPngEncoder::FilterFlag::kNone;
    opts.fZLibLevel = 1;
    SkDynamicMemoryWStream stream;
    if (!SkPngEncoder::Encode(&stream, pixmap, opts))
        return;
    sk_sp<SkData> data = stream.detachAsData();
    std::ofstream ostream(file, std::ios::binary);
    ostream.write(static_cast<const char*>(data->data()), data->size());
}

#ifdef DBG_UTIL
void prefillSurface(const sk_sp<SkSurface>& surface)
{
    // Pre-fill the surface with deterministic garbage.
    SkBitmap bitmap;
    bitmap.allocN32Pixels(2, 2);
    SkPMColor* scanline;
    scanline = bitmap.getAddr32(0, 0);
    *scanline++ = SkPreMultiplyARGB(0xFF, 0xBF, 0x80, 0x40);
    *scanline++ = SkPreMultiplyARGB(0xFF, 0x40, 0x80, 0xBF);
    scanline = bitmap.getAddr32(0, 1);
    *scanline++ = SkPreMultiplyARGB(0xFF, 0xE3, 0x5C, 0x13);
    *scanline++ = SkPreMultiplyARGB(0xFF, 0x13, 0x5C, 0xE3);
    bitmap.setImmutable();
    SkPaint paint;
    paint.setBlendMode(SkBlendMode::kSrc); // set as is, including alpha
    paint.setShader(
        bitmap.makeShader(SkTileMode::kRepeat, SkTileMode::kRepeat, SkSamplingOptions()));
    surface->getCanvas()->drawPaint(paint);
}
#endif

} // namespace

#endif // HAVE_FEATURE_SKIA

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
