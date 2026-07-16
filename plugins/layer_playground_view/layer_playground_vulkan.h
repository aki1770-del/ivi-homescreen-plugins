/*
 * Copyright 2026 Toyota Connected North America
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FLUTTER_PLUGIN_LAYER_PLAYGROUND_VULKAN_H_
#define FLUTTER_PLUGIN_LAYER_PLAYGROUND_VULKAN_H_

#include <cstdint>
#include <vector>

// C handle types only (VkImage/VkDevice/…) keep this header light; the .cc uses
// vulkan.hpp + the shared dynamic dispatcher. Both come from ivi-homescreen's
// vendored third_party/Vulkan-Headers via the plugin's include path.
#include <vulkan/vulkan_core.h>

namespace plugin_layer_playground_view {

// Optional Vulkan render path for the layer-playground platform view.
//
// The GL path renders with the *engine's* GL context, which does not exist on
// the wayland-vulkan backend. This path instead reuses the backend's Vulkan
// device (obtained via Backend::GetVulkanContext) and writes the same diagonal
// gradient into a linear, host-visible, dma-buf-exported VkImage — so the
// platform view's content is a first-class Vulkan resource on the exact device
// Flutter renders with, ready for the compositor to import (that consumption is
// a separate step). No second VkInstance/VkDevice, no cross-API copy.
class LayerPlaygroundVulkanRenderer {
 public:
  LayerPlaygroundVulkanRenderer() = default;
  ~LayerPlaygroundVulkanRenderer();

  LayerPlaygroundVulkanRenderer(const LayerPlaygroundVulkanRenderer&) = delete;
  LayerPlaygroundVulkanRenderer& operator=(
      const LayerPlaygroundVulkanRenderer&) = delete;

  // Adopt the backend's Vulkan handles (does not take ownership — the device is
  // Flutter's). Vulkan entry points come from the process-shared dynamic
  // dispatcher the backend already initialized, so no loader is passed.
  bool Init(VkInstance instance,
            VkPhysicalDevice physical_device,
            VkDevice device,
            VkQueue queue,
            uint32_t queue_family_index);

  // Ensure the image is at least @p width x @p height and filled with the
  // gradient. Returns true on success. Allocation is grow-only: the compositor
  // scales the whole image into the exact layer rect and the gradient is
  // normalized (identical at any scale), so a larger image serves any smaller
  // box. A box that shrinks — or grows within the current image — reuses it and
  // does no work; only a box that exceeds it enlarges, and the old image is
  // retired (freed a few frames later) instead of draining the device. This
  // keeps an animated resize from reallocating, and stalling, every frame.
  bool Render(int32_t width, int32_t height);

  [[nodiscard]] VkImage image() const { return image_; }
  [[nodiscard]] int32_t width() const { return width_; }
  [[nodiscard]] int32_t height() const { return height_; }
  [[nodiscard]] uint64_t row_pitch() const { return row_pitch_; }
  [[nodiscard]] bool ok() const { return device_ != VK_NULL_HANDLE; }

  // Current VkImageLayout (as uint32_t so the interface stays Vulkan-free).
  // Starts PREINITIALIZED after a fill; the compositor transitions it once to
  // a transfer source and records that back here, so the static image is not
  // re-transitioned every frame.
  [[nodiscard]] uint32_t layout() const { return layout_; }
  void set_layout(uint32_t layout) { layout_ = layout; }

 private:
  void DestroyImage();
  // Free images retired at least a few generations ago; by then any in-flight
  // compositor read that referenced them has completed. Called from Render on
  // the rasterizer thread (the same thread that submits), so the ordering is
  // safe without a device drain.
  void ReapRetired();

  // Borrowed (owned by the backend / Flutter).
  VkInstance instance_{VK_NULL_HANDLE};
  VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
  VkDevice device_{VK_NULL_HANDLE};
  VkQueue queue_{VK_NULL_HANDLE};
  uint32_t queue_family_index_{0};

  // Owned image resources (destroyed with the borrowed device). width_/height_
  // are the allocated (grow-only) dimensions, which the compositor scales to
  // the layer rect.
  VkImage image_{VK_NULL_HANDLE};
  VkDeviceMemory memory_{VK_NULL_HANDLE};
  int32_t width_{0};
  int32_t height_{0};
  uint64_t row_pitch_{0};
  bool painted_{false};

  // Images superseded by a grow, awaiting free once their generation has aged
  // out (see ReapRetired). Monotonic grow keeps this to a handful over a view's
  // lifetime.
  struct Retired {
    VkImage image;
    VkDeviceMemory memory;
    uint64_t gen;
  };
  std::vector<Retired> retired_;
  uint64_t gen_{0};
  // VK_IMAGE_LAYOUT_PREINITIALIZED after a fresh fill; updated by the
  // compositor.
  uint32_t layout_{0};
};

}  // namespace plugin_layer_playground_view

#endif  // FLUTTER_PLUGIN_LAYER_PLAYGROUND_VULKAN_H_
