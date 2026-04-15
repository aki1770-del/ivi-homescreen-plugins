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

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <GLES2/gl2.h>
#include <flutter/plugin_registrar.h>

#include "config/common.h"
#include "flutter_desktop_engine_state.h"
#include "flutter_homescreen.h"
#include "libnav_render.h"
#include "platform_views/platform_view.h"

#if BUILD_COMPOSITOR
#include "view/compositor_surface_interface.h"
#endif

namespace nav_render_view_plugin {

/**
 * Nav-render platform view backed by libnav_render's TextureRender API.
 *
 * Renders into an FBO using the engine's GL context (no per-plugin EGL
 * context, no Wayland subsurface). The compositor composites the
 * resulting GL_TEXTURE_2D into the scene at the layer's offset/size each
 * frame. When BUILD_COMPOSITOR is off the plugin still constructs but
 * does no rendering — compositor mode is a hard prerequisite for the
 * texture-mode path.
 */
class NavRenderSurface final : public flutter::Plugin,
                               public PlatformView
#if BUILD_COMPOSITOR
    , public ICompositorSurface
#endif
{
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrar* registrar,
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
                                    void* platform_view_context);

  NavRenderSurface(int32_t id,
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
                   void* platform_view_context);

  ~NavRenderSurface() override;

  NavRenderSurface(const NavRenderSurface&) = delete;
  NavRenderSurface& operator=(const NavRenderSurface&) = delete;

#if BUILD_COMPOSITOR
  // ICompositorSurface
  bool OnCreateBackingStore(const FlutterBackingStoreConfig*,
                            FlutterBackingStore*) override {
    return false;
  }
  bool OnCollectBackingStore(const FlutterBackingStore*) override {
    return true;
  }
  bool OnPresent(const FlutterLayer* layer) override;
  [[nodiscard]] FlutterPlatformViewIdentifier GetIdentifier() const override {
    return id_;
  }
  void OnResize(int32_t w, int32_t h) override;

  [[nodiscard]] uint32_t GetGlTextureName() const override {
    return color_texture_;
  }
  [[nodiscard]] int32_t GetGlTextureWidth() const override {
    return tex_width_;
  }
  [[nodiscard]] int32_t GetGlTextureHeight() const override {
    return tex_height_;
  }
#endif

 private:
  int32_t id_;
  void* platformViewsContext_;
  PlatformViewRemoveListener removeListener_;
  std::string flutterAssetsPath_;

#if BUILD_COMPOSITOR
  // libnav_render configuration captured in the constructor; consumed
  // when GL state is initialized lazily on the rasterizer thread.
  std::string access_token_;
  std::string asset_path_;
  std::string cache_folder_;
  std::string misc_folder_;

  nav_render_Context* context_{nullptr};
  bool gl_initialized_{false};
  bool init_failed_{false};
  GLuint framebuffer_{0};
  GLuint color_texture_{0};
  int32_t tex_width_{0};
  int32_t tex_height_{0};
  std::atomic<int32_t> pending_width_{0};
  std::atomic<int32_t> pending_height_{0};

  void EnsureGlState(int32_t w, int32_t h);
#endif

  static void on_resize(double width, double height, void* data);
  static void on_set_direction(int32_t direction, void* data);
  static void on_set_offset(double left, double top, void* data);
  static void on_touch(int32_t action,
                       int32_t point_count,
                       size_t point_data_size,
                       const double* point_data,
                       void* data);
  static void on_dispose(bool hybrid, void* data);

  static const struct platform_view_listener platform_view_listener_;
};

}  // namespace nav_render_view_plugin
