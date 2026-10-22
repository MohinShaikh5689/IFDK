/* iOS: FSR 3 (ffx_vk_portable) and FSR 4 (ffx_vk_fsr4_v07) are not built. Every entry point gow3gpu
 * uses fails, so vk_temporal_upscaler.cpp and vk_fsr4.cpp take their "unavailable" paths. */
#include <stddef.h>
#include "ffx/ffx_vk_fsr4_v07.h"
#include "ffx/ffx_vk_fsr4_v07_assets.h"
#include "ffx/ffx_vk_portable.h"

uint64_t ffxVkPortableValidateUpscaleCreateInfo(const FfxVkPortableUpscaleCreateInfo *createInfo) {
    (void)createInfo;
    return 0;
}

uint64_t ffxVkPortableValidateUpscaleDispatchInfo(const FfxVkPortableUpscaleCreateInfo *createInfo,
                                                  const FfxVkPortableUpscaleDispatchInfo *dispatchInfo) {
    (void)createInfo;
    (void)dispatchInfo;
    return 0;
}

FfxVkPortableResult ffxVkPortableUpscaleContextCreate(const FfxVkPortableDeviceInfo *deviceInfo,
                                                      const FfxVkPortableUpscaleCreateInfo *createInfo,
                                                      FfxVkPortableUpscaleContext **context) {
    (void)deviceInfo;
    (void)createInfo;
    if (context) *context = NULL;
    return FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
}

FfxVkPortableResult ffxVkPortableUpscaleContextGetMemoryUsage(FfxVkPortableUpscaleContext *context,
                                                              FfxVkPortableMemoryUsage *usage) {
    (void)context;
    (void)usage;
    return FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
}

FfxVkPortableResult ffxVkPortableUpscaleContextRecordDispatch(FfxVkPortableUpscaleContext *context,
                                                              const FfxVkPortableUpscaleDispatchInfo *dispatchInfo) {
    (void)context;
    (void)dispatchInfo;
    return FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
}

FfxVkPortableResult ffxVkPortableUpscaleContextDestroy(FfxVkPortableUpscaleContext *context) {
    (void)context;
    return FFX_VK_PORTABLE_OK;
}

const char *ffxVkPortableValidationIssueName(uint64_t singleIssueBit) {
    (void)singleIssueBit;
    return "unsupported";
}

void ffxFsr4V07SetBackendInterface(FfxInterface *backend) {
    (void)backend;
}

ffxReturnCode_t ffxFsr4V07CreateContext(ffxContext *ctx, ffxCreateContextDescHeader *desc,
                                        const ffxAllocationCallbacks *mem) {
    (void)ctx;
    (void)desc;
    (void)mem;
    return FFX_API_RETURN_ERROR;
}

ffxReturnCode_t ffxFsr4V07DestroyContext(ffxContext *ctx, const ffxAllocationCallbacks *mem) {
    (void)ctx;
    (void)mem;
    return FFX_API_RETURN_OK;
}

ffxReturnCode_t ffxFsr4V07Dispatch(ffxContext *ctx, const ffxDispatchDescHeader *desc) {
    (void)ctx;
    (void)desc;
    return FFX_API_RETURN_ERROR;
}

size_t ffxFsr4VkGetScratchMemorySize(void) {
    return 0;
}

VkResult ffxFsr4VkCreateContext(const FfxFsr4VkCreateInfo *createInfo, FfxInterface *outInterface) {
    (void)createInfo;
    (void)outInterface;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

void ffxFsr4VkDestroyContext(FfxFsr4VkContext *ctx) {
    (void)ctx;
}

VkResult ffxFsr4VkBeginFrame(FfxInterface *iface, uint64_t frameId) {
    (void)iface;
    (void)frameId;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VkResult ffxFsr4VkRetireFrame(FfxInterface *iface, uint64_t completedFrameId) {
    (void)iface;
    (void)completedFrameId;
    return VK_SUCCESS;
}

VkResult ffxFsr4VkSetExternalImageState(FfxInterface *iface, const FfxFsr4VkExternalImageState *state) {
    (void)iface;
    (void)state;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

bool ffxFsr4V07BuildAssetSet(FfxFsr4ModelPreset preset, uint32_t outputWidth, uint32_t outputHeight,
                             FfxFsr4V07AssetSet *outAssets) {
    (void)preset;
    (void)outputWidth;
    (void)outputHeight;
    (void)outAssets;
    return false;
}

const char *ffxFsr4ModelPresetName(FfxFsr4ModelPreset preset) {
    (void)preset;
    return "unsupported";
}
