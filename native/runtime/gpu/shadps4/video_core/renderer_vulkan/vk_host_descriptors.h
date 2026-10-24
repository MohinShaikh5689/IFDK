// SPDX-License-Identifier: GPL-2.0-or-later
// gow3 (iOS): the renderer's own passes (detiler, blits, fault processing, HLE shaders,
// post-processing) push their descriptors. MoltenVK 1.4.1+ leaves its buffer-size metadata stale
// for pushed descriptors (KytyPS5 #1083). With robustBufferAccess every storage buffer access is
// clamped to that size, so a stale size from an earlier, smaller dispatch cuts the detiler off
// part way through a large texture. When host_push_descriptors_off is set (vk_instance.h), these
// passes write a fresh descriptor set from a shared heap and bind it instead.

#pragma once

#include <span>

#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class Instance;
class Scheduler;

namespace HostDescriptors {

/// PipelineCache constructor: the device, and the GPU tick that recycles the heap's pools.
void Init(const Instance& instance, Scheduler& scheduler);

/// Flags for a set layout bound with Push: push descriptors, or none when they are off.
vk::DescriptorSetLayoutCreateFlags LayoutFlags();

/// cmdbuf.pushDescriptorSetKHR at set 0, or a fresh set with these writes bound there. Any
/// thread (the recording thread runs it inside Scheduler::Record).
void Push(vk::CommandBuffer cmdbuf, vk::PipelineBindPoint bind_point, vk::PipelineLayout layout,
          vk::DescriptorSetLayout set_layout, std::span<const vk::WriteDescriptorSet> writes);

} // namespace HostDescriptors

} // namespace Vulkan
