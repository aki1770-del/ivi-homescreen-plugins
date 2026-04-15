/*
 * Copyright 2020-2023 Toyota Connected North America
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

#include "layer_playground_view_plugin.h"

#include <flutter/standard_message_codec.h>

#include "plugins/common/common.h"
#include "view/flutter_view.h"

class FlutterView;
class Display;

namespace plugin_layer_playground_view {

void LayerPlaygroundViewPlugin::RegisterWithRegistrar(
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
  auto plugin = std::make_unique<LayerPlaygroundViewPlugin>(
      id, std::move(viewType), direction, top, left, width, height, params,
      std::move(assetDirectory), engine, addListener, removeListener,
      platform_view_context);
  registrar->AddPlugin(std::move(plugin));
}

LayerPlaygroundViewPlugin::LayerPlaygroundViewPlugin(
    int32_t id,
    std::string viewType,
    int32_t direction,
    double top,
    double left,
    double width,
    double height,
    const std::vector<uint8_t>& /* params */,
    std::string /* assetDirectory */,
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
      removeListener_(removeListener) {
  SPDLOG_TRACE("++LayerPlaygroundViewPlugin::LayerPlaygroundViewPlugin");

#if BUILD_COMPOSITOR
  pending_width_ = static_cast<int32_t>(width);
  pending_height_ = static_cast<int32_t>(height);

  // Register so PresentLayers routes layer dispatch to OnPresent. GL state
  // is allocated lazily on the rasterizer thread the first time OnPresent
  // fires — the engine's context isn't current here on the platform thread.
  if (state && state->view_controller && state->view_controller->view) {
    state->view_controller->view->RegisterCompositorSurface(
        id_,
        std::shared_ptr<ICompositorSurface>(this, [](ICompositorSurface*) {
          // Aliasing deleter: the plugin's lifetime is owned by the
          // PluginRegistrar, not by this shared_ptr. The compositor's
          // copy is dropped on UnregisterCompositorSurface.
        }));
  }
#else
  (void)state;
#endif

  addListener(platformViewsContext_, id, &platform_view_listener_, this);
  SPDLOG_TRACE("--LayerPlaygroundViewPlugin::LayerPlaygroundViewPlugin");
}

LayerPlaygroundViewPlugin::~LayerPlaygroundViewPlugin() {
  removeListener_(platformViewsContext_, id_);
}

void LayerPlaygroundViewPlugin::on_resize(double width,
                                          double height,
                                          void* data) {
  if (auto* plugin = static_cast<LayerPlaygroundViewPlugin*>(data)) {
    plugin->width_ = static_cast<int32_t>(width);
    plugin->height_ = static_cast<int32_t>(height);
#if BUILD_COMPOSITOR
    plugin->pending_width_ = static_cast<int32_t>(width);
    plugin->pending_height_ = static_cast<int32_t>(height);
#endif
    SPDLOG_TRACE("Resize: {} {}", width, height);
  }
}

void LayerPlaygroundViewPlugin::on_set_direction(const int32_t direction,
                                                 void* data) {
  if (auto* plugin = static_cast<LayerPlaygroundViewPlugin*>(data)) {
    plugin->direction_ = direction;
    SPDLOG_TRACE("SetDirection: {}", plugin->direction_);
  }
}

void LayerPlaygroundViewPlugin::on_set_offset(const double left,
                                              const double top,
                                              void* data) {
  // Offset positioning is handled by the compositor sequencer in
  // BUILD_COMPOSITOR mode; we just track the values for diagnostics.
  if (auto* plugin = static_cast<LayerPlaygroundViewPlugin*>(data)) {
    plugin->left_ = static_cast<int32_t>(left);
    plugin->top_ = static_cast<int32_t>(top);
    SPDLOG_TRACE("SetOffset: {} {}", left, top);
  }
}

void LayerPlaygroundViewPlugin::on_touch(int32_t /* action */,
                                         int32_t /* point_count */,
                                         const size_t /* point_data_size */,
                                         const double* /* point_data */,
                                         void* /* data */) {}

void LayerPlaygroundViewPlugin::on_dispose(bool /* hybrid */, void* /* data */) {
  // Compositor-owned shared_ptr drops via PlatformViewsHandler's
  // safety-net UnregisterCompositorSurface call. GL state is leaked
  // intentionally — destroying GL handles requires the engine's context
  // to be current on the rasterizer thread, and we have no way to
  // marshal that from the platform-thread dispose call. The engine's
  // context outlives the plugin and reclaims the resources at shutdown.
}

const platform_view_listener
    LayerPlaygroundViewPlugin::platform_view_listener_ = {
        .resize = on_resize,
        .set_direction = on_set_direction,
        .set_offset = on_set_offset,
        .on_touch = on_touch,
        .dispose = on_dispose,
        .accept_gesture = nullptr,
        .reject_gesture = nullptr,
};

#if BUILD_COMPOSITOR

namespace {

GLuint LoadShader(const GLchar* src, GLenum type) {
  const GLuint shader = glCreateShader(type);
  if (!shader) {
    return 0;
  }
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);
  GLint compiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    GLint len = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
    std::string log(static_cast<size_t>(len > 0 ? len : 0), '\0');
    if (len > 0) {
      glGetShaderInfoLog(shader, len, nullptr, log.data());
    }
    spdlog::error("LayerPlaygroundViewPlugin: shader compile failed: {}", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

constexpr GLchar kVertSrc[] =
    "attribute vec4 vPosition;\n"
    "void main() { gl_Position = vPosition; }\n";

constexpr GLchar kFragSrc[] =
    "precision mediump float;\n"
    "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";

}  // namespace

void LayerPlaygroundViewPlugin::EnsureGlState(int32_t w, int32_t h) {
  if (gl_initialized_ && w == tex_width_ && h == tex_height_) {
    return;
  }

  // Re-create the texture / FBO on size change. Program is reused across
  // resizes.
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
    spdlog::error(
        "LayerPlaygroundViewPlugin: FBO incomplete at {}x{}", w, h);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);

  if (!program_) {
    const GLuint vs = LoadShader(kVertSrc, GL_VERTEX_SHADER);
    const GLuint fs = LoadShader(kFragSrc, GL_FRAGMENT_SHADER);
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glBindAttribLocation(program_, 0, "vPosition");
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked) {
      GLint len = 0;
      glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &len);
      std::string log(static_cast<size_t>(len > 0 ? len : 0), '\0');
      if (len > 0) {
        glGetProgramInfoLog(program_, len, nullptr, log.data());
      }
      spdlog::error("LayerPlaygroundViewPlugin: program link failed: {}", log);
      glDeleteProgram(program_);
      program_ = 0;
    }
  }

  gl_initialized_ = (program_ != 0);
}

void LayerPlaygroundViewPlugin::DestroyGlState() {
  // Intentionally not called from the platform thread — see on_dispose.
  if (color_texture_) {
    glDeleteTextures(1, &color_texture_);
    color_texture_ = 0;
  }
  if (framebuffer_) {
    glDeleteFramebuffers(1, &framebuffer_);
    framebuffer_ = 0;
  }
  if (program_) {
    glDeleteProgram(program_);
    program_ = 0;
  }
  gl_initialized_ = false;
}

void LayerPlaygroundViewPlugin::DrawFrame() const {
  static constexpr GLfloat verts[] = {0.0f,  0.5f, 0.0f, -0.5f, -0.5f,
                                      0.0f,  0.5f, -0.5f, 0.0f};

  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glViewport(0, 0, tex_width_, tex_height_);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(program_);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, verts);
  glEnableVertexAttribArray(0);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDisableVertexAttribArray(0);
  glUseProgram(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool LayerPlaygroundViewPlugin::OnPresent(const FlutterLayer* layer) {
  // Apply any pending resize from the platform thread, then render.
  const int32_t target_w = pending_width_.load();
  const int32_t target_h = pending_height_.load();
  if (target_w <= 0 || target_h <= 0) {
    return true;
  }
  EnsureGlState(target_w, target_h);
  if (!gl_initialized_) {
    return false;
  }
  DrawFrame();
  (void)layer;
  return true;
}

void LayerPlaygroundViewPlugin::OnResize(int32_t w, int32_t h) {
  pending_width_ = w;
  pending_height_ = h;
}

#endif  // BUILD_COMPOSITOR

}  // namespace plugin_layer_playground_view
