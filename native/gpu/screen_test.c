/* SPDX-License-Identifier: GPL-3.0-or-later
 * Milestone 5, step 1: MoltenVK on the iPhone's screen. A Vulkan swapchain on the app's CAMetalLayer
 * is cleared to a cycling colour for a few seconds, and the report lists what shadPS4's video core
 * (gow3_android gpu/shadps4/video_core/renderer_vulkan/vk_instance.cpp) asks of the device. */
#ifndef __APPLE__ /* clock_gettime on glibc (Linux syntax checks) */
#define _POSIX_C_SOURCE 200809L
#endif
#define VK_USE_PLATFORM_METAL_EXT
#include <vulkan/vulkan.h>
#include "gow3core.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    char *out;
    size_t capacity, length;
} Json;

static void put(Json *j, const char *format, ...) {
    va_list args;
    va_start(args, format);
    char *dst = j->length < j->capacity ? j->out + j->length : NULL;
    size_t room = j->length < j->capacity ? j->capacity - j->length : 0;
    int n = vsnprintf(dst, room, format, args);
    va_end(args);
    if (n > 0) j->length += (size_t)n;
}

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

/* shadPS4 asserts on the first four; it uses the others when the device has them. */
enum { REQUIRED_EXTENSIONS = 4 };
static const char *const shadps4_extensions[] = {
    "VK_KHR_swapchain", "VK_KHR_push_descriptor", "VK_EXT_vertex_attribute_divisor", "VK_EXT_robustness2",
    "VK_KHR_maintenance5", "VK_KHR_maintenance8", "VK_KHR_pipeline_binary", "VK_EXT_extended_dynamic_state3",
    "VK_EXT_vertex_input_dynamic_state", "VK_EXT_custom_border_color", "VK_EXT_depth_clip_control",
    "VK_EXT_depth_clip_enable", "VK_EXT_depth_range_unrestricted", "VK_EXT_primitive_topology_list_restart",
    "VK_EXT_provoking_vertex", "VK_EXT_shader_stencil_export", "VK_EXT_image_2d_view_of_3d",
    "VK_EXT_image_view_min_lod", "VK_EXT_shader_atomic_float", "VK_EXT_shader_atomic_float2",
    "VK_KHR_fragment_shader_barycentric", "VK_KHR_compute_shader_derivatives", "VK_KHR_shader_clock",
    "VK_KHR_workgroup_memory_explicit_layout", "VK_EXT_attachment_feedback_loop_layout",
    "VK_EXT_attachment_feedback_loop_dynamic_state", "VK_EXT_memory_budget", "VK_EXT_calibrated_timestamps",
    "VK_KHR_portability_subset",
};

static int has_extension(const VkExtensionProperties *list, uint32_t count, const char *name) {
    for (uint32_t i = 0; i < count; ++i)
        if (!strcmp(list[i].extensionName, name)) return 1;
    return 0;
}

size_t gow3_gpu_screen_test(void *metal_layer, uint32_t width, uint32_t height, unsigned seconds, char *out, size_t capacity) {
    Json j = {out, capacity, 0};
    const char *step = "instance";
    VkResult r = VK_SUCCESS;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSemaphore *rendered = NULL;
    VkImage *images = NULL;
    VkExtensionProperties *extensions = NULL;
    uint32_t image_count = 0, frames = 0;
    double elapsed = 0;
    if (out && capacity) out[0] = 0;
    put(&j, "{");

#define TRY(what, call)                       \
    do {                                      \
        step = (what);                        \
        if ((r = (call)) != VK_SUCCESS) goto done; \
    } while (0)

    /* Instance: the Metal surface, and portability enumeration (MoltenVK is a portability driver). */
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &n, NULL);
    VkExtensionProperties *instance_extensions = calloc(n ? n : 1, sizeof(*instance_extensions));
    vkEnumerateInstanceExtensionProperties(NULL, &n, instance_extensions);
    const int portability = has_extension(instance_extensions, n, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    free(instance_extensions);
    const char *instance_names[3] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_METAL_SURFACE_EXTENSION_NAME,
                                     VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "gow3-ios screen test",
                             .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
                                .enabledExtensionCount = portability ? 3 : 2, .ppEnabledExtensionNames = instance_names,
                                .flags = portability ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0};
    TRY("vkCreateInstance", vkCreateInstance(&ici, NULL, &instance));

    VkMetalSurfaceCreateInfoEXT sci = {.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT, .pLayer = metal_layer};
    TRY("vkCreateMetalSurfaceEXT", vkCreateMetalSurfaceEXT(instance, &sci, NULL, &surface));

    VkPhysicalDevice physical;
    n = 1;
    step = "vkEnumeratePhysicalDevices";
    r = vkEnumeratePhysicalDevices(instance, &n, &physical);
    if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || !n) {
        if (r == VK_SUCCESS) r = VK_ERROR_INITIALIZATION_FAILED;
        goto done;
    }
    r = VK_SUCCESS;

    /* What the device is and what shadPS4 would get from it. */
    VkPhysicalDeviceDriverProperties driver = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &driver};
    vkGetPhysicalDeviceProperties2(physical, &properties);
    const uint32_t api = properties.properties.apiVersion;
    put(&j, "\"device\":\"%s\",\"driver\":\"%s %s\",\"api\":\"%u.%u.%u\",\"api_ok_for_shadps4\":%s,",
        properties.properties.deviceName, driver.driverName, driver.driverInfo, VK_API_VERSION_MAJOR(api),
        VK_API_VERSION_MINOR(api), VK_API_VERSION_PATCH(api), api >= VK_API_VERSION_1_3 ? "true" : "false");

    vkEnumerateDeviceExtensionProperties(physical, NULL, &n, NULL);
    extensions = calloc(n ? n : 1, sizeof(*extensions));
    vkEnumerateDeviceExtensionProperties(physical, NULL, &n, extensions);
    const uint32_t extension_count = n;
    put(&j, "\"extension_count\":%u,\"missing_required\":[", extension_count);
    int first = 1;
    for (int i = 0; i < REQUIRED_EXTENSIONS; ++i)
        if (!has_extension(extensions, extension_count, shadps4_extensions[i])) {
            put(&j, "%s\"%s\"", first ? "" : ",", shadps4_extensions[i]);
            first = 0;
        }
    put(&j, "],\"missing_optional\":[");
    first = 1;
    for (size_t i = REQUIRED_EXTENSIONS; i < sizeof(shadps4_extensions) / sizeof(*shadps4_extensions); ++i)
        if (!has_extension(extensions, extension_count, shadps4_extensions[i])) {
            put(&j, "%s\"%s\"", first ? "" : ",", shadps4_extensions[i]);
            first = 0;
        }
    put(&j, "],");

    VkPhysicalDeviceRobustness2FeaturesEXT robustness2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
    VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    if (has_extension(extensions, extension_count, "VK_EXT_robustness2")) features.pNext = &robustness2;
    vkGetPhysicalDeviceFeatures2(physical, &features);
    const VkPhysicalDeviceFeatures *f = &features.features;
    put(&j, "\"features\":{\"geometryShader\":%d,\"tessellationShader\":%d,\"dualSrcBlend\":%d,\"logicOp\":%d,"
            "\"depthClamp\":%d,\"depthBiasClamp\":%d,\"fillModeNonSolid\":%d,\"imageCubeArray\":%d,\"independentBlend\":%d,"
            "\"sampleRateShading\":%d,\"multiViewport\":%d,\"textureCompressionBC\":%d,\"shaderInt64\":%d,"
            "\"shaderInt16\":%d,\"shaderFloat64\":%d,\"fragmentStoresAndAtomics\":%d,\"vertexPipelineStoresAndAtomics\":%d,"
            "\"shaderStorageImageExtendedFormats\":%d,\"robustBufferAccess2\":%d,\"robustImageAccess2\":%d,\"nullDescriptor\":%d},",
        f->geometryShader, f->tessellationShader, f->dualSrcBlend, f->logicOp, f->depthClamp, f->depthBiasClamp,
        f->fillModeNonSolid, f->imageCubeArray, f->independentBlend, f->sampleRateShading, f->multiViewport,
        f->textureCompressionBC, f->shaderInt64, f->shaderInt16, f->shaderFloat64, f->fragmentStoresAndAtomics,
        f->vertexPipelineStoresAndAtomics, f->shaderStorageImageExtendedFormats, robustness2.robustBufferAccess2,
        robustness2.robustImageAccess2, robustness2.nullDescriptor);

    /* A queue that draws and presents. */
    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, NULL);
    VkQueueFamilyProperties family_list[16];
    if (families > 16) families = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, family_list);
    uint32_t family = families;
    for (uint32_t i = 0; i < families && family == families; ++i) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &present);
        if ((family_list[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) family = i;
    }
    step = "queue family";
    if (family == families) {
        r = VK_ERROR_FEATURE_NOT_PRESENT;
        goto done;
    }

    const char *device_names[2] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, "VK_KHR_portability_subset"};
    const int subset = has_extension(extensions, extension_count, "VK_KHR_portability_subset");
    float priority = 1;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family,
                                   .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
                              .pQueueCreateInfos = &qci, .enabledExtensionCount = subset ? 2 : 1,
                              .ppEnabledExtensionNames = device_names};
    TRY("vkCreateDevice", vkCreateDevice(physical, &dci, NULL, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);

    /* Swapchain: cleared with a transfer each frame, so it needs TRANSFER_DST. */
    VkSurfaceCapabilitiesKHR caps;
    TRY("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps));
    step = "swapchain usage";
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        r = VK_ERROR_FEATURE_NOT_PRESENT;
        goto done;
    }
    VkSurfaceFormatKHR formats[32];
    n = 32;
    step = "vkGetPhysicalDeviceSurfaceFormatsKHR";
    r = vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, formats);
    if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || !n) goto done;
    r = VK_SUCCESS;
    VkSurfaceFormatKHR format = formats[0];
    for (uint32_t i = 0; i < n; ++i)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) format = formats[i];
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) extent = (VkExtent2D){width, height};
    uint32_t wanted_images = caps.minImageCount < 3 ? 3 : caps.minImageCount;
    if (caps.maxImageCount && wanted_images > caps.maxImageCount) wanted_images = caps.maxImageCount;
    VkSwapchainCreateInfoKHR swci = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surface,
                                     .minImageCount = wanted_images, .imageFormat = format.format,
                                     .imageColorSpace = format.colorSpace, .imageExtent = extent, .imageArrayLayers = 1,
                                     .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                     .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform = caps.currentTransform,
                                     .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                     .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE};
    TRY("vkCreateSwapchainKHR", vkCreateSwapchainKHR(device, &swci, NULL, &swapchain));
    TRY("vkGetSwapchainImagesKHR", vkGetSwapchainImagesKHR(device, swapchain, &image_count, NULL));
    images = calloc(image_count, sizeof(*images));
    rendered = calloc(image_count, sizeof(*rendered));
    TRY("vkGetSwapchainImagesKHR", vkGetSwapchainImagesKHR(device, swapchain, &image_count, images));
    put(&j, "\"swapchain\":{\"width\":%u,\"height\":%u,\"format\":%d,\"images\":%u},", extent.width, extent.height,
        (int)format.format, image_count);

    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                   .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = family};
    TRY("vkCreateCommandPool", vkCreateCommandPool(device, &pci, NULL, &pool));
    VkCommandBuffer command;
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                                       .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    TRY("vkAllocateCommandBuffers", vkAllocateCommandBuffers(device, &cai, &command));
    VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    TRY("vkCreateFence", vkCreateFence(device, &fci, NULL, &fence));
    VkSemaphoreCreateInfo semi = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    TRY("vkCreateSemaphore", vkCreateSemaphore(device, &semi, NULL, &acquired));
    for (uint32_t i = 0; i < image_count; ++i) TRY("vkCreateSemaphore", vkCreateSemaphore(device, &semi, NULL, &rendered[i]));

    /* Frames: one at a time (the fence is waited before the command buffer is reused). */
    const double start = now();
    while ((elapsed = now() - start) < (double)seconds) {
        uint32_t index;
        TRY("vkAcquireNextImageKHR", vkAcquireNextImageKHR(device, swapchain, 1000000000ull, acquired, VK_NULL_HANDLE, &index));
        TRY("vkResetCommandBuffer", vkResetCommandBuffer(command, 0));
        VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                          .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        TRY("vkBeginCommandBuffer", vkBeginCommandBuffer(command, &begin));
        VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageMemoryBarrier to_clear = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                         .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                         .image = images[index], .subresourceRange = range};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &to_clear);
        const float t = (float)elapsed * 2.0f;
        VkClearColorValue colour = {.float32 = {0.5f + 0.5f * sinf(t), 0.5f + 0.5f * sinf(t + 2.094f), 0.5f + 0.5f * sinf(t + 4.189f), 1.0f}};
        vkCmdClearColorImage(command, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &colour, 1, &range);
        VkImageMemoryBarrier to_present = to_clear;
        to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_present.dstAccessMask = 0;
        to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &to_present);
        TRY("vkEndCommandBuffer", vkEndCommandBuffer(command));
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &acquired,
                               .pWaitDstStageMask = &wait_stage, .commandBufferCount = 1, .pCommandBuffers = &command,
                               .signalSemaphoreCount = 1, .pSignalSemaphores = &rendered[index]};
        TRY("vkQueueSubmit", vkQueueSubmit(queue, 1, &submit, fence));
        VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1,
                                    .pWaitSemaphores = &rendered[index], .swapchainCount = 1, .pSwapchains = &swapchain,
                                    .pImageIndices = &index};
        TRY("vkQueuePresentKHR", vkQueuePresentKHR(queue, &present));
        TRY("vkWaitForFences", vkWaitForFences(device, 1, &fence, VK_TRUE, 1000000000ull));
        TRY("vkResetFences", vkResetFences(device, 1, &fence));
        ++frames;
    }
    step = "done";
#undef TRY

done:
    put(&j, "\"frames\":%u,\"seconds\":%.2f,\"fps\":%.1f,\"step\":\"%s\",\"vk_result\":%d,\"ok\":%s}", frames, elapsed,
        elapsed > 0 ? frames / elapsed : 0.0, step, (int)r, r == VK_SUCCESS && frames > 0 ? "true" : "false");
    if (device) vkDeviceWaitIdle(device);
    if (rendered)
        for (uint32_t i = 0; i < image_count; ++i)
            if (rendered[i]) vkDestroySemaphore(device, rendered[i], NULL);
    if (acquired) vkDestroySemaphore(device, acquired, NULL);
    if (fence) vkDestroyFence(device, fence, NULL);
    if (pool) vkDestroyCommandPool(device, pool, NULL);
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, NULL);
    if (device) vkDestroyDevice(device, NULL);
    if (surface) vkDestroySurfaceKHR(instance, surface, NULL);
    if (instance) vkDestroyInstance(instance, NULL);
    free(rendered);
    free(images);
    free(extensions);
    return j.length;
}
