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

// ihs_pv (push-model) producer for the layer_playground demo platform view.
//
// Registers a process-global factory for "@views/simple-box-view-type" that
// negotiates a surface path (dma-buf import, or the software-shm floor) through
// ihs_pv_negotiate and submits a solid, per-id colored buffer — the box the
// compositor scans out. This migrates the demo view onto the shell
// PlatformViewRegistry factory + ihs_pv ABI; because the factory is registered
// at startup, the channel handler routes creates to it and the legacy
// ICompositorSurface path in layer_playground_view_plugin.cc goes unused.
//
// The content is a static solid color (one per view id, with a thin border), so
// the producer submits once on create and again on each resize — the compositor
// keeps compositing the last-submitted buffer every frame. Rendering a gradient
// into the dma-buf (GL into an EGLImage FBO) is a later polish; the negotiate +
// submit path is what this exercises.

#include "config/common.h"

#if BUILD_COMPOSITOR
#include <sys/mman.h>  // MAP_FAILED

#include <cstdint>
#include <cstdio>
#include <mutex>

#include <gbm.h>

#include "ihs/platform_view.h"
#endif  // BUILD_COMPOSITOR

namespace plugin_layer_playground_view {

#if BUILD_COMPOSITOR
namespace {

constexpr char kViewType[] = "@views/simple-box-view-type";

// XRGB8888 little-endian is 0xXX_RR_GG_BB; the top (X) byte is ignored by the
// KMS/GL sampler for an X format, but keep it 0xFF so the same buffer reads
// opaque if ever consumed as ARGB. One color per id so boxes are identifiable.
constexpr uint32_t kPalette[10] = {
    0xFFFF0000u,  // 0 red
    0xFF00C000u,  // 1 green
    0xFF0000FFu,  // 2 blue
    0xFFFFC000u,  // 3 amber
    0xFFFF00FFu,  // 4 magenta
    0xFF00E0E0u,  // 5 cyan
    0xFFFF8000u,  // 6 orange
    0xFF8000FFu,  // 7 violet
    0xFFFFFFFFu,  // 8 white
    0xFF808080u,  // 9 gray
};
constexpr uint32_t kBorder = 0xFF202020u;

// The imported buffer keeps a stable ring id: the registry re-imports on a size
// change even for the same id, so a single id avoids leaking one import per
// resize while still picking up new buffers.
constexpr uint32_t kBufferId = 0;

// Per-view producer. All entry points (Start from the factory, Resize/Dispose
// from IhsPvCallbacks) run on the platform thread, as do ihs_pv_egl_context /
// ihs_pv_negotiate / ihs_pv_submit — so no cross-thread synchronization of the
// gbm buffer is needed; the mutex only guards against a redundant reentrant
// produce.
class Producer {
 public:
  Producer(IhsPlatformView* view, int32_t id, uint32_t width, uint32_t height)
      : view_(view), id_(id), width_(width), height_(height) {}

  ~Producer() {
    if (bo_ != nullptr) {
      gbm_bo_destroy(bo_);
    }
  }

  Producer(const Producer&) = delete;
  Producer& operator=(const Producer&) = delete;

  void Start() {
    if (!Negotiate()) {
      return;
    }
    Produce();
  }

  void Resize(uint32_t width, uint32_t height) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (width == width_ && height == height_) {
      return;
    }
    width_ = width;
    height_ = height;
    Produce();
  }

 private:
  bool Negotiate() {
    IhsPvRequirements req{};
    req.struct_size = sizeof(req);
    // Offer the zero-copy import and the universal floor; the scorer picks the
    // best the active backend grants (dma-buf on wayland/drm-egl, shm floor on
    // the software backend). No format list: a solid buffer takes any format.
    req.kinds = IHS_PV_KIND_TEXTURE_DMABUF_IMPORT | IHS_PV_KIND_SOFTWARE_SHM;
    req.formats = nullptr;
    req.format_count = 0;
    req.needs_alpha = 0;              // opaque box
    req.sync = IHS_PV_SYNC_IMPLICIT;  // static CPU-filled buffer, no fencing
    req.z_order = IHS_PV_Z_BELOW_FLUTTER;

    IhsPvGrant grant{};
    grant.struct_size = sizeof(grant);
    const int rc = ihs_pv_negotiate(view_, &req, &grant);
    if (rc != IHS_PV_OK) {
      std::fprintf(stderr, "[layer_playground] negotiate failed id=%d rc=%d\n",
                   id_, rc);
      return false;
    }
    granted_kind_ = grant.granted_kind;
    return true;
  }

  // Allocate a solid-color gbm buffer at the current size and submit it. Caller
  // holds mutex_ (or is the single-threaded Start path).
  void Produce() {
    if (granted_kind_ != IHS_PV_KIND_TEXTURE_DMABUF_IMPORT) {
      // The software-shm floor is produced through ihs_pv_grant_shm_fd; wiring
      // that (and the host-side shm composite) is a separate step. On an
      // EGL/Vulkan backend the grant is dma-buf import, so this is not hit.
      return;
    }
    // Flutter hands a 1x1 placeholder at create; skip until a real resize.
    if (width_ <= 1 || height_ <= 1) {
      return;
    }

    IhsEglContext egl{};
    egl.struct_size = sizeof(egl);
    if (ihs_pv_egl_context(&egl) != IHS_PV_OK || egl.gbm_device == nullptr) {
      return;
    }
    auto* dev = static_cast<gbm_device*>(egl.gbm_device);

    // GBM_FORMAT_XRGB8888 == DRM_FORMAT_XRGB8888 (same fourcc); SCANOUT |
    // LINEAR so the buffer is both KMS-scanout-capable and CPU-mappable to
    // fill.
    gbm_bo* bo = gbm_bo_create(dev, width_, height_, GBM_FORMAT_XRGB8888,
                               GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);
    if (bo == nullptr) {
      std::fprintf(stderr,
                   "[layer_playground] gbm_bo_create failed id=%d %ux%u\n", id_,
                   width_, height_);
      return;
    }
    FillSolid(bo);

    const int fd = gbm_bo_get_fd(bo);
    if (fd < 0) {
      gbm_bo_destroy(bo);
      return;
    }

    IhsFrame frame{};
    frame.struct_size = sizeof(frame);
    frame.format.fourcc = gbm_bo_get_format(bo);
    frame.format.modifier = gbm_bo_get_modifier(bo);
    frame.width = width_;
    frame.height = height_;
    frame.plane_count = 1;
    frame.plane_fd[0] = fd;  // ownership passes to the registry on import
    frame.plane_offset[0] = gbm_bo_get_offset(bo, 0);
    frame.plane_stride[0] = gbm_bo_get_stride(bo);
    frame.buffer_id = kBufferId;
    // Implicit sync: gbm_bo_map/unmap completed the CPU write before submit.
    ihs_pv_submit(view_, &frame, /*acquire_fence_fd=*/-1,
                  /*out_release_fence_fd=*/nullptr);

    // The new buffer is imported (the host re-imports on the size change); free
    // the previous one. The exported fd is owned by the registry now, and the
    // underlying dma-buf keeps the memory alive independent of this handle.
    if (bo_ != nullptr) {
      gbm_bo_destroy(bo_);
    }
    bo_ = bo;
  }

  void FillSolid(gbm_bo* bo) const {
    uint32_t stride = 0;
    void* map_data = nullptr;
    void* px = gbm_bo_map(bo, 0, 0, width_, height_, GBM_BO_TRANSFER_WRITE,
                          &stride, &map_data);
    if (px == nullptr || px == MAP_FAILED) {
      return;
    }
    const uint32_t fill = kPalette[static_cast<uint32_t>((id_ % 10 + 10) % 10)];
    auto* base = static_cast<uint8_t*>(px);
    for (uint32_t y = 0; y < height_; ++y) {
      auto* row = reinterpret_cast<uint32_t*>(base + y * stride);
      for (uint32_t x = 0; x < width_; ++x) {
        const bool edge = x < 2 || y < 2 || x + 2 >= width_ || y + 2 >= height_;
        row[x] = edge ? kBorder : fill;
      }
    }
    gbm_bo_unmap(bo, map_data);
  }

  IhsPlatformView* view_;
  int32_t id_;
  uint32_t width_;
  uint32_t height_;
  uint32_t granted_kind_{IHS_PV_KIND_NONE};
  gbm_bo* bo_{nullptr};
  std::mutex mutex_;
};

int Factory(const IhsPvCreateInfo* info,
            void* /*factory_user_data*/,
            IhsPlatformView* view,
            IhsPvCallbacks* out_callbacks,
            void** out_user_data) {
  auto* producer =
      new Producer(view, info->id, static_cast<uint32_t>(info->width),
                   static_cast<uint32_t>(info->height));
  out_callbacks->struct_size = sizeof(*out_callbacks);
  out_callbacks->resize = [](void* ud, double w, double h) {
    static_cast<Producer*>(ud)->Resize(static_cast<uint32_t>(w),
                                       static_cast<uint32_t>(h));
  };
  out_callbacks->dispose = [](void* ud) { delete static_cast<Producer*>(ud); };
  *out_user_data = producer;
  producer->Start();
  return IHS_PV_OK;
}

}  // namespace

// Registered at startup from the generated plugin registrant, after the ihs_pv
// host is installed (SetUpCommonEngineState runs before
// PluginsApiRegisterPlugins in flutter_view.cc). Process-global; the factory
// binds to the installed host.
void RegisterIhsPvFactory() {
  ihs_pv_register_factory(kViewType, &Factory, nullptr);
}

#else  // !BUILD_COMPOSITOR

// Platform views compose through the compositor; without it the factory has
// nothing to register against, so this is a no-op and the registrant can call
// it unconditionally.
void RegisterIhsPvFactory() {}

#endif  // BUILD_COMPOSITOR

}  // namespace plugin_layer_playground_view
