// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_host_descriptors.h"

#include <array>
#include <memory>
#include <mutex>

#include <boost/container/small_vector.hpp>

#include "common/assert.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan::HostDescriptors {

namespace {

constexpr std::array PoolSizes = {
    vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 512},
    vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 1024},
    vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 512},
    vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 512},
    vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 512},
    vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 256},
    vk::DescriptorPoolSize{vk::DescriptorType::eUniformTexelBuffer, 128},
    vk::DescriptorPoolSize{vk::DescriptorType::eStorageTexelBuffer, 128},
};

std::mutex mutex;
vk::Device device;
std::unique_ptr<DescriptorHeap> heap;

bool Off() {
    return host_push_descriptors_off.load(std::memory_order_relaxed);
}

} // namespace

void Init(const Instance& instance, Scheduler& scheduler) {
    std::scoped_lock lock{mutex};
    if (!heap) {
        device = instance.GetDevice();
        heap = std::make_unique<DescriptorHeap>(instance, scheduler.GetWorkSemaphore(), PoolSizes,
                                                256);
    }
}

vk::DescriptorSetLayoutCreateFlags LayoutFlags() {
    return Off() ? vk::DescriptorSetLayoutCreateFlags{}
                 : vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR;
}

void Push(vk::CommandBuffer cmdbuf, vk::PipelineBindPoint bind_point, vk::PipelineLayout layout,
          vk::DescriptorSetLayout set_layout, std::span<const vk::WriteDescriptorSet> writes) {
    if (!Off()) {
        cmdbuf.pushDescriptorSetKHR(bind_point, layout, 0, writes);
        return;
    }
    vk::DescriptorSet set;
    {
        std::scoped_lock lock{mutex};
        ASSERT_MSG(heap, "HostDescriptors::Init was not called");
        set = heap->Commit(set_layout);
        boost::container::small_vector<vk::WriteDescriptorSet, 8> bound(writes.begin(),
                                                                        writes.end());
        for (auto& write : bound) {
            write.dstSet = set;
        }
        device.updateDescriptorSets(bound, {});
    }
    cmdbuf.bindDescriptorSets(bind_point, layout, 0, set, {});
}

} // namespace Vulkan::HostDescriptors
