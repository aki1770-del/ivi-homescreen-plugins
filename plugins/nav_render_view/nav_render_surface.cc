/*
 * Copyright 2021-2026 Toyota Connected North America
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

#include "nav_render_surface.h"

#include <flutter/standard_message_codec.h>
#include <plugins/common/common.h>
#include <memory>

#include <utility>

#include "libnav_render.h"

#if BUILD_COMPOSITOR
#include "view/flutter_view.h"
#endif

namespace nav_render_view_plugin {

void NavRenderSurface::RegisterWithRegistrar(
    flutter::PluginRegistrar* registrar,
    int32_t id,
    std::string viewType,
    int32_t direction,
    double top,
    double left,
    double width,
    double height,
    const std::vector<uint8_t>& params,
    std::string assetDirectory,
    FlutterDesktopEngineRef engine,
    PlatformViewAddListener addListener,
    PlatformViewRemoveListener removeListener,
    void* platform_view_context) {
  auto plugin = std::make_unique<NavRenderSurface>(
      id, std::move(viewType), direction, top, left, width, height, params,
      std::move(assetDirectory), engine, addListener, removeListener,
      platform_view_context);
  registrar->AddPlugin(std::move(plugin));
}

NavRenderSurface::NavRenderSurface(int32_t id,
                                   std::string viewType,
                                   int32_t direction,
                                   double top,
                                   double left,
                                   double width,
                                   double height,
                                   const std::vector<uint8_t>& params,
                                   std::string assetDirectory,
                                   FlutterDesktopEngineState* state,
                                   PlatformViewAddListener addListener,
                                   PlatformViewRemoveListener removeListener,
                                   void* platform_view_context)
    : PlatformView(id,
                   std::move(viewType),
                   direction,
                   top,
                   left,
                   width,
                   height),
      id_(id),
      platformViewsContext_(platform_view_context),
      removeListener_(removeListener),
      flutterAssetsPath_(std::move(assetDirectory)) {
  IHS_TRACE("++NavRenderSurface::NavRenderSurface");

  auto& codec = flutter::StandardMessageCodec::GetInstance();
  const auto decoded = codec.DecodeMessage(params.data(), params.size());
  const auto& creationParams =
      std::get_if<flutter::EncodableMap>(decoded.get());

  std::string access_token;
  std::string asset_path;
  std::string cache_folder;
  std::string misc_folder;
  bool map_flutter_assets{};
  if (creationParams) {
    for (const auto& [fst, snd] : *creationParams) {
      const auto key = std::get<std::string>(fst);
      if (key == "access_token" && std::holds_alternative<std::string>(snd)) {
        access_token = std::get<std::string>(snd);
      } else if (key == "map_flutter_assets" &&
                 std::holds_alternative<bool>(snd)) {
        map_flutter_assets = std::get<bool>(snd);
      } else if (key == "asset_path" &&
                 std::holds_alternative<std::string>(snd)) {
        asset_path = std::get<std::string>(snd);
      } else if (key == "cache_folder" &&
                 std::holds_alternative<std::string>(snd)) {
        cache_folder = std::get<std::string>(snd);
      } else if (key == "misc_folder" &&
                 std::holds_alternative<std::string>(snd)) {
        misc_folder = std::get<std::string>(snd);
      }
    }
  }
  if (map_flutter_assets) {
    asset_path = flutterAssetsPath_;
  }

  if (!LibNavRender::IsPresent()) {
    ihs::log::error("[NavRenderViewPlugin] libnav_render.so missing");
    return;
  }

#if BUILD_COMPOSITOR
  if (LibNavRender::kExpectedTextureApiVersion !=
      LibNavRender->TextureGetInterfaceVersion()) {
    ihs::log::error(
        "[NavRenderViewPlugin] unexpected texture API version: {} (need {})",
        LibNavRender->TextureGetInterfaceVersion(),
        LibNavRender::kExpectedTextureApiVersion);
    return;
  }

  access_token_ = std::move(access_token);
  asset_path_ = std::move(asset_path);
  cache_folder_ = std::move(cache_folder);
  misc_folder_ = std::move(misc_folder);
  pending_width_ = static_cast<int32_t>(width);
  pending_height_ = static_cast<int32_t>(height);

  if (state && state->view_controller && state->view_controller->view) {
    state->view_controller->view->RegisterCompositorSurface(
        id_, std::shared_ptr<ICompositorSurface>(this, [](ICompositorSurface*) {
          // Aliasing deleter — PluginRegistrar owns the plugin.
        }));
    IHS_TRACE("[pv-trace] NavRenderSurface registered: id={} size={}x{}", id_,
              pending_width_.load(), pending_height_.load());
  } else {
    IHS_TRACE(
        "[pv-trace] NavRenderSurface could NOT register (state/view null): "
        "id={}",
        id_);
  }
#else
  (void)state;
  (void)access_token;
  (void)asset_path;
  (void)cache_folder;
  (void)misc_folder;
  ihs::log::warn(
      "[NavRenderViewPlugin] BUILD_COMPOSITOR is off; plugin will not "
      "render. Rebuild with -DBUILD_COMPOSITOR=ON.");
#endif

  addListener(platformViewsContext_, id, &platform_view_listener_, this);
  IHS_TRACE("--NavRenderSurface::NavRenderSurface");
}

NavRenderSurface::~NavRenderSurface() {
  removeListener_(platformViewsContext_, id_);
}

void NavRenderSurface::on_resize(double width, double height, void* data) {
  if (auto* p = static_cast<NavRenderSurface*>(data)) {
    p->width_ = static_cast<int32_t>(width);
    p->height_ = static_cast<int32_t>(height);
#if BUILD_COMPOSITOR
    p->pending_width_ = static_cast<int32_t>(width);
    p->pending_height_ = static_cast<int32_t>(height);
#endif
    IHS_TRACE("[NavRenderSurface] on_resize: {} {}", width, height);
  }
}

void NavRenderSurface::on_set_direction(int32_t direction, void* data) {
  if (auto* p = static_cast<NavRenderSurface*>(data)) {
    p->direction_ = direction;
    IHS_TRACE("[NavRenderSurface] on_set_direction: {}", direction);
  }
}

void NavRenderSurface::on_set_offset(double left, double top, void* data) {
  if (auto* p = static_cast<NavRenderSurface*>(data)) {
    p->left_ = static_cast<int32_t>(left);
    p->top_ = static_cast<int32_t>(top);
    IHS_TRACE("[NavRenderSurface] on_set_offset: {} {}", left, top);
  }
}

void NavRenderSurface::on_touch(int32_t /*action*/,
                                int32_t /*point_count*/,
                                size_t /*point_data_size*/,
                                const double* /*point_data*/,
                                void* /*data*/) {}

void NavRenderSurface::on_dispose(bool /*hybrid*/, void* /*data*/) {
  // GL state and the libnav_render context are intentionally leaked —
  // tearing them down requires the engine's GL context current on the
  // rasterizer thread, and there's no clean way to marshal that from
  // the platform-thread dispose call. The engine context outlives the
  // plugin and reclaims the resources at shutdown.
  // PlatformViewsHandler invokes UnregisterCompositorSurface for us.
}

const platform_view_listener NavRenderSurface::platform_view_listener_ = {
    .resize = on_resize,
    .set_direction = on_set_direction,
    .set_offset = on_set_offset,
    .on_touch = on_touch,
    .dispose = on_dispose,
    .accept_gesture = nullptr,
    .reject_gesture = nullptr,
};

#if BUILD_COMPOSITOR

void NavRenderSurface::EnsureGlState(int32_t w, int32_t h) {
  if (init_failed_) {
    return;
  }
  if (gl_initialized_ && w == tex_width_ && h == tex_height_) {
    return;
  }

  // Recreate the FBO + texture on size change. The libnav_render context
  // is told to resize so it re-allocates internal G-buffers.
  if (color_texture_) {
    glDeleteTextures(1, &color_texture_);
    color_texture_ = 0;
  }
  if (framebuffer_) {
    glDeleteFramebuffers(1, &framebuffer_);
    framebuffer_ = 0;
  }

  tex_width_ = w;
  tex_height_ = h;

  glGenTextures(1, &color_texture_);
  glBindTexture(GL_TEXTURE_2D, color_texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_width_, tex_height_, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);

  glGenFramebuffers(1, &framebuffer_);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         color_texture_, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    ihs::log::error("[NavRenderSurface] FBO incomplete at {}x{}", w, h);
    init_failed_ = true;
    return;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);

  if (!context_) {
    context_ = LibNavRender->TextureInitialize(
        access_token_.c_str(), tex_width_, tex_height_, asset_path_.c_str(),
        cache_folder_.c_str(), misc_folder_.c_str());
    if (!context_) {
      ihs::log::error("[NavRenderSurface] TextureInitialize failed");
      init_failed_ = true;
      return;
    }
  } else if (LibNavRender->TextureResize) {
    LibNavRender->TextureResize(context_, tex_width_, tex_height_);
  }

  gl_initialized_ = true;
}

bool NavRenderSurface::OnPresent(const FlutterLayer* layer) {
  if (init_failed_) {
    return false;
  }
  // The engine supplies the composed layer size every frame in layer->size;
  // trust that over on_resize, which the embedder may never deliver while the
  // widget is laid out (the platform-views `create` message carries 1x1
  // placeholders). Fall back to pending_*/ atomic if the layer is absent.
  int32_t target_w = pending_width_.load();
  int32_t target_h = pending_height_.load();
  if (layer && layer->size.width > 0 && layer->size.height > 0) {
    target_w = static_cast<int32_t>(layer->size.width);
    target_h = static_cast<int32_t>(layer->size.height);
  }
  static thread_local int32_t last_w = 0;
  static thread_local int32_t last_h = 0;
  static thread_local bool first_fire = true;
  const bool size_changed = (target_w != last_w) || (target_h != last_h);
  if (first_fire || size_changed) {
    IHS_TRACE(
        "[pv-trace] NavRenderSurface::OnPresent id={} target={}x{} "
        "tex={}x{} gl_init={} (first={}, size_changed={})",
        id_, target_w, target_h, tex_width_, tex_height_, gl_initialized_,
        first_fire, size_changed);
    first_fire = false;
    last_w = target_w;
    last_h = target_h;
  }
  if (target_w <= 0 || target_h <= 0) {
    return true;
  }

  EnsureGlState(target_w, target_h);
  if (!gl_initialized_ || !context_) {
    IHS_TRACE(
        "[pv-trace] NavRenderSurface::OnPresent id={} gl NOT initialised "
        "(context/FBO init failed?); returning false",
        id_);
    return false;
  }

  if (LibNavRender->TextureRunTask) {
    LibNavRender->TextureRunTask(context_);
  }
  // libnav_render renders into the framebuffer we provide. After this
  // call returns, color_texture_ contains the latest map frame; the
  // compositor will composite it into the scene.
  LibNavRender->TextureRender(context_, framebuffer_);
  return true;
}

void NavRenderSurface::OnResize(int32_t w, int32_t h) {
  pending_width_ = w;
  pending_height_ = h;
}

#endif  // BUILD_COMPOSITOR

}  // namespace nav_render_view_plugin
