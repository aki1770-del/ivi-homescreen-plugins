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
//
// LP_PV_DMABUF_FOURCC=nv12 switches the submitted buffer to NV12 (the palette
// color converted to BT.709 limited-range Y'CbCr), exercising the shell's YUV
// direct-scanout path: the compositor marks it ContentType::Video and programs
// the KMS plane CSC (COLOR_ENCODING/COLOR_RANGE) from the frame's
// color_space/color_range. Default is the solid XRGB8888 box (the RGB path).

#include "config/common.h"

#if BUILD_COMPOSITOR
#include <fcntl.h>      // O_CLOEXEC
#include <sys/ioctl.h>  // ioctl
#include <sys/mman.h>   // mmap / munmap / MAP_FAILED

#include <drm/drm.h>  // DRM_IOCTL_MODE_{CREATE,MAP,DESTROY}_DUMB, PRIME_HANDLE_TO_FD

#include <cstdint>
#include <cstdio>
#include <cstdlib>  // getenv
#include <cstring>  // memset
#include <mutex>
#include <string_view>

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

// LP_PV_DMABUF_FOURCC=nv12 selects the YUV plane path; anything else (or unset)
// keeps the XRGB8888 box. Read once per producer.
bool WantNv12() {
  const char* e = std::getenv("LP_PV_DMABUF_FOURCC");
  return e != nullptr &&
         (std::string_view(e) == "nv12" || std::string_view(e) == "NV12");
}

// LP_PV_DRM_PLANE=1 asks for the explicit DRM_PLANE grant instead of
// TEXTURE_DMABUF_IMPORT, so ihs_pv_grant_drm_plane_id reports the KMS plane the
// view is scanned out on (0 while GL-composited). Both kinds submit a dma-buf
// the same way; this only changes which grant is requested (and the accessor).
bool WantDrmPlane() {
  const char* e = std::getenv("LP_PV_DRM_PLANE");
  return e != nullptr && std::string_view(e) == "1";
}

// LP_PV_HDR=1 tags the NV12 frame as HDR10 (BT.2020 / PQ) with the mastering
// metadata below, so the shell signals it on the connector's
// HDR_OUTPUT_METADATA. Implies the NV12 (YUV) path.
bool WantHdr() {
  const char* e = std::getenv("LP_PV_HDR");
  return e != nullptr && std::string_view(e) == "1";
}

// HDR10 mastering metadata for the LP_PV_HDR test frame: BT.2020 primaries +
// D65 white (CIE xy in 0.00002 units), PQ transfer, 1000/0.0001 cd/m^2
// mastering luminance, MaxCLL 1000 / MaxFALL 400. Positional aggregate init
// (C++17): the field order matches IhsHdrMetadata.
constexpr IhsHdrMetadata kHdr10{
    sizeof(IhsHdrMetadata),  // struct_size
    IHS_TRANSFER_PQ,         // transfer
    {0, 0, 0, 0, 0, 0, 0},   // reserved[7]
    {35400, 8500, 6550},     // display_primaries_x R,G,B (BT.2020)
    {14600, 39850, 2300},    // display_primaries_y
    15635,                   // white_point_x (D65)
    16450,                   // white_point_y
    1000,                    // max_display_mastering_luminance cd/m^2
    1,                       // min_display_mastering_luminance 0.0001
    1000,                    // max_content_light_level (MaxCLL)
    400,                     // max_frame_average_light_level (MaxFALL)
};

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
    const bool size_changed = width != width_ || height != height_;
    width_ = width;
    height_ = height;
    // The RGB box is a static producer (submit once per size). NV12 resubmits
    // on every present (the DRM compositor calls OnResize each frame) so it
    // acts as a continuous producer: the host's GetDmabuf re-arms on each
    // submit, so the frame keeps direct-scanning-out on a KMS plane (exercising
    // the YUV plane CSC). A one-shot submit would be consumed by the first
    // GL-fallback present and, being static, never return to scanout.
    if (size_changed || want_nv12_) {
      Produce();
    }
  }

 private:
  bool Negotiate() {
    IhsPvRequirements req{};
    req.struct_size = sizeof(req);
    // DRM_PLANE (when requested) or the zero-copy dma-buf import, plus the
    // universal shm floor; the scorer picks the best the active backend grants
    // (dma-buf/plane on drm-egl, shm floor on software). No format list: a
    // solid buffer takes any format.
    req.kinds = (want_drm_plane_ ? IHS_PV_KIND_DRM_PLANE
                                 : IHS_PV_KIND_TEXTURE_DMABUF_IMPORT) |
                IHS_PV_KIND_SOFTWARE_SHM;
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

  // Allocate the current-size buffer and submit it. RGB goes through gbm; NV12
  // goes through a DRM dumb buffer (gbm_bo_create can't allocate planar YUV on
  // the drivers we run — vc4/amdgpu/vkms). Caller holds mutex_ (or is the
  // single-threaded Start path).
  void Produce() {
    // Both the dma-buf import and the DRM_PLANE grant submit a dma-buf the same
    // way; only the shm floor needs a different path (ihs_pv_grant_shm_fd),
    // which isn't wired yet. On an EGL/Vulkan backend the grant is one of the
    // dma-buf kinds, so the early return is not hit there.
    if (granted_kind_ != IHS_PV_KIND_TEXTURE_DMABUF_IMPORT &&
        granted_kind_ != IHS_PV_KIND_DRM_PLANE) {
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
    if (want_nv12_) {
      ProduceNv12(dev);
    } else {
      ProduceRgb(dev);
    }
    // Read back the DRM_PLANE grant accessor: the KMS plane this view is
    // scanned out on (0 while GL-composited, or not a DRM_PLANE grant).
    // Throttled so a continuous producer doesn't spam.
    if (want_drm_plane_ && (produce_count_++ % 120) == 0) {
      std::fprintf(stderr, "[layer_playground] id=%d drm_plane_id=%u\n", id_,
                   ihs_pv_grant_drm_plane_id(view_));
    }
  }

  // Solid per-id XRGB8888 box through gbm. SCANOUT | LINEAR keeps it both
  // KMS-scannable and CPU-mappable; GBM_FORMAT_XRGB8888 == DRM_FORMAT_XRGB8888.
  void ProduceRgb(gbm_device* dev) {
    gbm_bo* bo = gbm_bo_create(dev, width_, height_, GBM_FORMAT_XRGB8888,
                               GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);
    if (bo == nullptr) {
      std::fprintf(stderr,
                   "[layer_playground] gbm_bo_create failed id=%d %ux%u\n", id_,
                   width_, height_);
      return;
    }
    const int fd = gbm_bo_get_fd(bo);
    if (fd < 0) {
      gbm_bo_destroy(bo);
      return;
    }
    FillSolid(bo);

    IhsFrame frame{};
    frame.struct_size = sizeof(frame);
    frame.format.fourcc = gbm_bo_get_format(bo);
    frame.format.modifier = gbm_bo_get_modifier(bo);
    frame.width = width_;
    frame.height = height_;
    frame.plane_count = 1;
    frame.plane_fd[0] = fd;  // ownership passes to the registry on import
    frame.plane_offset[0] = gbm_bo_get_offset(bo, 0);
    frame.plane_stride[0] = gbm_bo_get_stride_for_plane(bo, 0);
    frame.buffer_id = kBufferId;
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

  // NV12 through a DRM dumb buffer: allocate one linear BO on the gbm device's
  // scanout fd (Y rows then interleaved UV rows), fill it with the palette
  // color as BT.709 limited Y'CbCr, PRIME-export it, and submit as NV12 so the
  // shell programs the KMS plane CSC. Destroying the GEM handle right after
  // export is safe — the exported dma-buf holds its own reference to the
  // memory.
  void ProduceNv12(gbm_device* dev) {
    const int drm_fd = gbm_device_get_fd(dev);
    if (drm_fd < 0) {
      return;
    }
    // Y plane (height_ rows) + interleaved UV plane (height_/2 rows) at 8
    // bits/sample; pitch is the shared per-row stride the driver picks.
    drm_mode_create_dumb creq{};
    creq.width = width_;
    creq.height = height_ + height_ / 2;
    creq.bpp = 8;
    if (::ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) != 0) {
      std::fprintf(stderr,
                   "[layer_playground] CREATE_DUMB NV12 failed id=%d %ux%u\n",
                   id_, width_, height_);
      return;
    }

    drm_mode_map_dumb mreq{};
    mreq.handle = creq.handle;
    if (::ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) == 0) {
      void* m = ::mmap(nullptr, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                       drm_fd, static_cast<off_t>(mreq.offset));
      if (m != MAP_FAILED) {
        FillNv12(static_cast<uint8_t*>(m), creq.pitch);
        ::munmap(m, creq.size);
      } else {
        std::fprintf(stderr, "[layer_playground] NV12 dumb mmap failed id=%d\n",
                     id_);
      }
    }

    drm_prime_handle ph{};
    ph.handle = creq.handle;
    ph.flags = O_CLOEXEC;
    const bool exported =
        ::ioctl(drm_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph) == 0 && ph.fd >= 0;

    // Drop the GEM handle now; the PRIME dma-buf keeps its own reference.
    drm_mode_destroy_dumb dreq{};
    dreq.handle = creq.handle;
    ::ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
    if (!exported) {
      std::fprintf(stderr,
                   "[layer_playground] PRIME export NV12 failed id=%d\n", id_);
      return;
    }

    IhsFrame frame{};
    frame.struct_size = sizeof(frame);
    frame.format.fourcc = GBM_FORMAT_NV12;  // == DRM_FORMAT_NV12
    frame.format.modifier = 0;              // DRM_FORMAT_MOD_LINEAR
    frame.width = width_;
    frame.height = height_;
    // HDR is BT.2020 + PQ with mastering metadata; SDR is BT.709. Both limited.
    frame.color_space = want_hdr_ ? 3 : 2;  // BT2020 : BT709
    frame.color_range = 2;                  // IHS_COLOR_RANGE_LIMITED
    frame.hdr = want_hdr_ ? &kHdr10 : nullptr;
    // Y and UV share one dma-buf; the same fd repeats per plane (the IhsFrame
    // contract — the host dedups on close and dups per plane on import).
    frame.plane_count = 2;
    frame.plane_fd[0] = ph.fd;  // ownership passes to the registry on import
    frame.plane_fd[1] = ph.fd;
    frame.plane_offset[0] = 0;
    frame.plane_stride[0] = creq.pitch;
    frame.plane_offset[1] = creq.pitch * height_;
    frame.plane_stride[1] = creq.pitch;
    frame.buffer_id = kBufferId;
    ihs_pv_submit(view_, &frame, /*acquire_fence_fd=*/-1,
                  /*out_release_fence_fd=*/nullptr);

    // NV12 keeps no gbm bo; drop any stale one from an earlier RGB submit.
    if (bo_ != nullptr) {
      gbm_bo_destroy(bo_);
      bo_ = nullptr;
    }
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

  // Write the view's palette color as BT.709 limited-range Y'CbCr into a linear
  // NV12 buffer mapped at @base: the Y plane (height_ rows) then the
  // interleaved UV plane (height_/2 rows), both at @pitch stride. If the plane
  // CSC is programmed right the box decodes back to the RGB path's color — so
  // this validates the CSC, not just placement.
  void FillNv12(uint8_t* base, uint32_t pitch) const {
    const uint32_t rgb = kPalette[static_cast<uint32_t>((id_ % 10 + 10) % 10)];
    const auto r = static_cast<double>((rgb >> 16) & 0xFF);
    const auto g = static_cast<double>((rgb >> 8) & 0xFF);
    const auto b = static_cast<double>(rgb & 0xFF);
    auto clamp8 = [](double v) {
      return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
    };
    const uint8_t yv = clamp8(16.0 + 0.1826 * r + 0.6142 * g + 0.0620 * b);
    const uint8_t cu = clamp8(128.0 - 0.1006 * r - 0.3386 * g + 0.4392 * b);
    const uint8_t cv = clamp8(128.0 + 0.4392 * r - 0.3989 * g - 0.0403 * b);
    for (uint32_t y = 0; y < height_; ++y) {
      std::memset(base + static_cast<size_t>(y) * pitch, yv, width_);
    }
    uint8_t* uv = base + static_cast<size_t>(pitch) * height_;
    for (uint32_t y = 0; y < height_ / 2; ++y) {
      uint8_t* uvrow = uv + static_cast<size_t>(y) * pitch;
      for (uint32_t x = 0; x < width_ / 2; ++x) {
        uvrow[2 * x] = cu;
        uvrow[2 * x + 1] = cv;
      }
    }
  }

  IhsPlatformView* view_;
  int32_t id_;
  uint32_t width_;
  uint32_t height_;
  const bool want_hdr_{WantHdr()};
  const bool want_nv12_{WantNv12() || want_hdr_};  // HDR rides the YUV path
  const bool want_drm_plane_{WantDrmPlane()};
  uint32_t produce_count_{0};  // throttles the DRM_PLANE readback log
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
