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
#include <memory>

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
    const std::string& assetDirectory,
    FlutterDesktopEngineRef engine,
    PlatformViewAddListener addListener,
    PlatformViewRemoveListener removeListener,
    void* platform_view_context) {
  auto plugin = std::make_unique<LayerPlaygroundViewPlugin>(
      id, std::move(viewType), direction, top, left, width, height, params,
      assetDirectory, engine, addListener, removeListener,
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
    const std::string& /* assetDirectory */,
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
  IHS_TRACE("++LayerPlaygroundViewPlugin::LayerPlaygroundViewPlugin");

#if BUILD_COMPOSITOR
  pending_width_ = static_cast<int32_t>(width);
  pending_height_ = static_cast<int32_t>(height);

  // Register so PresentLayers routes layer dispatch to OnPresent. GL state
  // is allocated lazily on the rasterizer thread the first time OnPresent
  // fires — the engine's context isn't current here on the platform thread.
  if (state && state->view_controller && state->view_controller->view) {
    state->view_controller->view->RegisterCompositorSurface(
        id_, std::shared_ptr<ICompositorSurface>(this, [](ICompositorSurface*) {
          // Aliasing deleter: the plugin's lifetime is owned by the
          // PluginRegistrar, not by this shared_ptr. The compositor's
          // copy is dropped on UnregisterCompositorSurface.
        }));
    IHS_TRACE("[pv-trace] LayerPlaygroundView registered: id={} size={}x{}",
              id_, pending_width_.load(), pending_height_.load());
  } else {
    IHS_TRACE(
        "[pv-trace] LayerPlaygroundView could NOT register (state/view null): "
        "id={}",
        id_);
  }
#else
  (void)state;
#endif

  addListener(platformViewsContext_, id, &platform_view_listener_, this);
  IHS_TRACE("--LayerPlaygroundViewPlugin::LayerPlaygroundViewPlugin");
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
    IHS_TRACE("Resize: {} {}", width, height);
  }
}

void LayerPlaygroundViewPlugin::on_set_direction(const int32_t direction,
                                                 void* data) {
  if (auto* plugin = static_cast<LayerPlaygroundViewPlugin*>(data)) {
    plugin->direction_ = direction;
    IHS_TRACE("SetDirection: {}", plugin->direction_);
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
    IHS_TRACE("SetOffset: {} {}", left, top);
  }
}

void LayerPlaygroundViewPlugin::on_touch(int32_t /* action */,
                                         int32_t /* point_count */,
                                         const size_t /* point_data_size */,
                                         const double* /* point_data */,
                                         void* /* data */) {}

void LayerPlaygroundViewPlugin::on_dispose(bool /* hybrid */,
                                           void* /* data */) {
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
    ihs::log::error("LayerPlaygroundViewPlugin: shader compile failed: {}",
                    log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

// Mirrors the simple_box_plugin Cairo pattern: a diagonal 3-stop gradient
// (deep purple → warm coral → peach), a thin orange border, and a faint
// 20-pixel white grid. Label text from the GTK variant is omitted — GLES2
// font rendering would require a glyph atlas we don't want to pull in.
constexpr GLchar kVertSrc[] =
    "attribute vec2 aPosition;\n"
    "varying vec2 vUv;\n"
    "void main() {\n"
    "  vUv = aPosition * 0.5 + 0.5;\n"
    "  gl_Position = vec4(aPosition, 0.0, 1.0);\n"
    "}\n";

constexpr GLchar kFragSrc[] =
    "precision mediump float;\n"
    "uniform vec2 uResolution;\n"
    "varying vec2 vUv;\n"
    "vec3 gradient(float t) {\n"
    "  vec3 c0 = vec3(0.227, 0.110, 0.443);\n"  // #3A1C71
    "  vec3 c1 = vec3(0.843, 0.427, 0.467);\n"  // #D76D77
    "  vec3 c2 = vec3(1.000, 0.686, 0.482);\n"  // #FFAF7B
    "  return t < 0.5 ? mix(c0, c1, t * 2.0) : mix(c1, c2, (t - 0.5) * 2.0);\n"
    "}\n"
    "void main() {\n"
    "  vec2 px = vUv * uResolution;\n"
    "  float t = clamp((vUv.x + vUv.y) * 0.5, 0.0, 1.0);\n"
    "  vec3 color = gradient(t);\n"
    "  vec2 d = min(px, uResolution - px);\n"
    "  float edge = min(d.x, d.y);\n"
    "  if (edge < 2.0) {\n"
    "    color = mix(color, vec3(1.0, 0.65, 0.0), 0.9);\n"
    "  }\n"
    "  vec2 g = fract(px / 20.0) * 20.0;\n"
    "  float grid = step(g.x, 1.0) + step(g.y, 1.0);\n"
    "  color = mix(color, vec3(1.0), 0.12 * clamp(grid, 0.0, 1.0));\n"
    "  gl_FragColor = vec4(color, 1.0);\n"
    "}\n";

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
    ihs::log::error("LayerPlaygroundViewPlugin: FBO incomplete at {}x{}", w, h);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);

  if (!program_) {
    const GLuint vs = LoadShader(kVertSrc, GL_VERTEX_SHADER);
    const GLuint fs = LoadShader(kFragSrc, GL_FRAGMENT_SHADER);
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glBindAttribLocation(program_, 0, "aPosition");
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
      ihs::log::error("LayerPlaygroundViewPlugin: program link failed: {}",
                      log);
      glDeleteProgram(program_);
      program_ = 0;
    } else {
      u_resolution_loc_ = glGetUniformLocation(program_, "uResolution");
      a_position_loc_ = glGetAttribLocation(program_, "aPosition");
    }
  }

  // A VBO is required: under GLES3 the engine has a VAO bound, and
  // client-side vertex arrays are invalid unless the default VAO (0) is
  // current. A VBO works regardless of which VAO is bound.
  if (!vbo_ && program_) {
    static constexpr GLfloat kVerts[] = {
        -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f,
    };
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kVerts), kVerts, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  gl_initialized_ = (program_ != 0 && vbo_ != 0);
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
  if (vbo_) {
    glDeleteBuffers(1, &vbo_);
    vbo_ = 0;
  }
  if (program_) {
    glDeleteProgram(program_);
    program_ = 0;
  }
  gl_initialized_ = false;
}

void LayerPlaygroundViewPlugin::DrawFrame() const {
  // Flutter's Skia backend leaves assorted GL state on: a bound VAO, depth
  // test, scissor test, blend, colour/depth masks. Reset everything we care
  // about so the draw lands in the FBO as expected.
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glViewport(0, 0, tex_width_, tex_height_);

  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_STENCIL_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthMask(GL_FALSE);

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(program_);
  if (u_resolution_loc_ >= 0) {
    glUniform2f(u_resolution_loc_, static_cast<GLfloat>(tex_width_),
                static_cast<GLfloat>(tex_height_));
  }
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  const GLuint loc =
      a_position_loc_ >= 0 ? static_cast<GLuint>(a_position_loc_) : 0u;
  glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(loc);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(loc);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool LayerPlaygroundViewPlugin::OnPresent(const FlutterLayer* layer) {
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
        "[pv-trace] LayerPlaygroundView::OnPresent id={} target={}x{} "
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
  if (!gl_initialized_) {
    IHS_TRACE(
        "[pv-trace] LayerPlaygroundView::OnPresent id={} gl NOT initialised "
        "(program link failed?); returning false",
        id_);
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
