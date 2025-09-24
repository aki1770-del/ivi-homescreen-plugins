/*
 * Copyright 2020-2024 Toyota Connected North America
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

#include "camera_plugin.h"
#include <flutter/plugin_registrar_homescreen.h>
#include <jpeglib.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "CameraManager.h"
#include "plugins/common/common.h"
#include <flutter/standard_method_codec.h>
#include <flutter/event_stream_handler_functions.h>
#include <glib.h>  // for g_timeout_add (only for the optional fake generator)
#include <fstream>
#include <jpeglib.h>

extern "C" {
#include <pipewire/pipewire.h>
}

struct CameraInfo {
  uint32_t id;
  std::string name;
};

// Global vector to store camera info
std::vector<CameraInfo> cameras;

// Callback function for detecting cameras
void on_global(void* /*data*/,
               uint32_t id,
               uint32_t /*permissions*/,
               const char* /*type*/,
               uint32_t /*version*/,
               const struct spa_dict* props) {
  if (!props)
    return;

  const char* media_class = spa_dict_lookup(props, "media.class");
  const char* name = spa_dict_lookup(props, "node.description");

  if (media_class && std::string(media_class) == "Video/Source") {
    spdlog::debug("found camera: {} (id: {})", name, id);
    cameras.push_back({id, name ? name : "Unknown"});
  }
}

using namespace plugin_common;

namespace camera_plugin {

void CameraPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarDesktop* registrar) {
  auto plugin =
      std::make_unique<CameraPlugin>(registrar, registrar->messenger());
  SetUp(registrar->messenger(), plugin.get());
  registrar->AddPlugin(std::move(plugin));
}

CameraPlugin::CameraPlugin(flutter::PluginRegistrarDesktop* plugin_registrar,
                           flutter::BinaryMessenger* /*messenger*/)
  : mPreview(), registrar_(plugin_registrar) {
  if (!CameraManager::instance().initialize()) {
    spdlog::error("failed to initialize PipeWire manager!");
  }

    // ---- Create EventChannel for image stream (must match Dart name) ----
    auto messenger = registrar_->messenger();
    image_channel_ = std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
        messenger, "camera_linux/image_stream",
        &flutter::StandardMethodCodec::GetInstance());

    auto handler = std::make_unique<
        flutter::StreamHandlerFunctions<flutter::EncodableValue>>(
        /* on_listen */ [this](
            const flutter::EncodableValue* /*args*/,
            std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&& sink)
            -> std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>> {
          spdlog::info("[camera_plugin] image_stream on_listen");
          image_stream_active_ = true;
          image_sink_ = std::move(sink);
          StartImageStream();  // start PipeWire or a test generator
          return nullptr;
        },
        /* on_cancel */ [this](
            const flutter::EncodableValue* /*args*/)
            -> std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>> {
          spdlog::info("[camera_plugin] image_stream on_cancel");
          image_stream_active_ = false;
          StopImageStream();
          image_sink_.reset();
          return nullptr;
        });

    image_channel_->SetStreamHandler(std::move(handler));
}

CameraPlugin::~CameraPlugin() {
  CameraManager::instance().shutdown();
}

ErrorOr<flutter::EncodableList> CameraPlugin::GetAvailableCameras() {
  flutter::EncodableList list;
  const auto& mgr = CameraManager::instance();
  auto cameras = mgr.getAvailableCameras();
  for (const auto& [id, name] : cameras) {
    spdlog::debug("[camera_plugin] detected camera:  {} (camera_id: {})", name,
                  id);
    list.emplace_back(std::to_string(id));
  }
  return ErrorOr<flutter::EncodableList>(std::move(list));
}

void CameraPlugin::Create(
    const std::string& camera_id,
    const PlatformMediaSettings& /*settings*/,
    const std::function<void(ErrorOr<int64_t> reply)> result) {
  spdlog::debug("[camera_plugin] create camera_id: {}", camera_id);
  if (CameraId_CameraStream.find(camera_id) == CameraId_CameraStream.end()) {
    auto new_camera =
        std::make_shared<CameraStream>(registrar_, camera_id, 640, 480);

    new_camera->on_image_frame = [this](const uint8_t* y, int ys,
                               const uint8_t* u_or_uv, int us,
                               const uint8_t* v, int vs,
                               int w, int h,
                               const char* raw) {
      if (!image_stream_active_ || !image_sink_) return;

      // SAFETY: hop to the main loop before touching image_sink_.
      struct Payload {
        CameraPlugin* self;
        std::vector<uint8_t> y, u, v;
        int ys, us, vs, w, h;
        std::string raw;
      };
      auto p = new Payload{
        this,
        std::vector<uint8_t>(y, y + ys * h),
        std::vector<uint8_t>(u_or_uv, u_or_uv + us * (h/2)),
        (v ? std::vector<uint8_t>(v, v + vs * (h/2)) : std::vector<uint8_t>()),
        ys, us, vs, w, h, raw ? std::string(raw) : std::string("I420")
      };
      // Post to main thread (GLib). If you don't use GLib, queue & post some other way.
      g_main_context_invoke(nullptr, [](gpointer data) -> gboolean {
        std::unique_ptr<Payload> P(static_cast<Payload*>(data));
        if (!P->self->image_sink_) return G_SOURCE_REMOVE;

        if (P->raw == "NV12") {
          P->self->SendNV12Frame(P->y.data(), P->ys, P->u.data(), P->us, P->w, P->h);
        } else {
          P->self->SendI420Frame(P->y.data(), P->ys, P->u.data(), P->us,
                                 P->v.data(), P->vs, P->w, P->h);
        }
        return G_SOURCE_REMOVE;
      }, p);
    };

    CameraId_CameraStream.insert({camera_id, new_camera});
    TextureId_CameraStream.insert({new_camera->texture_id(), new_camera});
  }
  int64_t texture_id = CameraId_CameraStream[camera_id]->texture_id();
  spdlog::debug("[camera_plugin] camera_id {}'s texture_id: {}", camera_id,
                texture_id);
  result(ErrorOr<int64_t>(texture_id));

}

/******************************************************************************
 * decode_mjpeg
 ******************************************************************************/
int decode_mjpeg(const uint8_t* input,
                 size_t input_size,
                 uint8_t* output,
                 int out_width,
                 int out_height) {
  jpeg_decompress_struct cinfo{};
  jpeg_error_mgr jerr{};

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);

  jpeg_mem_src(&cinfo, input, input_size);
  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
    spdlog::error("[decode_mjpeg] failed to read JPEG header.");
    jpeg_destroy_decompress(&cinfo);
    return -1;
  }

  jpeg_start_decompress(&cinfo);
  if (cinfo.output_width != static_cast<uint32_t>(out_width) ||
      cinfo.output_height != static_cast<uint32_t>(out_height) ||
      cinfo.output_components != 3) {
    spdlog::error("[decode_mjpeg] unexpected size/components.");
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return -1;
  }

  const unsigned int row_stride = cinfo.output_width * cinfo.output_components;
  while (cinfo.output_scanline < cinfo.output_height) {
    JSAMPROW row[1];
    row[0] = &output[cinfo.output_scanline * row_stride];
    jpeg_read_scanlines(&cinfo, row, 1);
  }

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  return 0;
}
/******************************************************************************
 * parse_props_param: Dump all properties from a SPA_TYPE_OBJECT_Props param
 *
 * This function attempts to read each property key (like SPA_PROP_brightness)
 * from the param, then prints its value type. Real code might do more detailed
 * checks or convert to a known range.
 ******************************************************************************/

void save_image_to_jpeg(const std::string& filename,
                        const unsigned char* image_data,
                        const int width,
                        const int height,
                        const int channels,
                        const int quality) {
  jpeg_compress_struct cinfo {};
  jpeg_error_mgr jerr {};

  // Setup error handling
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);

  // Open file for writing
  FILE* outfile = fopen(filename.c_str(), "wb");
  if (!outfile) {
    spdlog::error("error: unable to open file {} for writing!", filename);
    return;
  }

  jpeg_stdio_dest(&cinfo, outfile);

  // Set image properties
  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = channels;
  cinfo.in_color_space = JCS_RGB;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, quality, TRUE);

  // Start compression
  jpeg_start_compress(&cinfo, TRUE);

  // Write scanlines
  JSAMPROW row_pointer;
  while (cinfo.next_scanline < cinfo.image_height) {
    row_pointer = const_cast<JSAMPROW>(&image_data[cinfo.next_scanline * width * channels]);
    jpeg_write_scanlines(&cinfo, &row_pointer, 1);
  }

  // Finish compression
  jpeg_finish_compress(&cinfo);
  fclose(outfile);
  jpeg_destroy_compress(&cinfo);
  spdlog::debug("image saved to {}", filename);
}

void CameraPlugin::Initialize(
    const int64_t texture_id,
    const std::function<void(ErrorOr<PlatformSize> reply)> result) {
  if (TextureId_CameraStream.find(texture_id) == TextureId_CameraStream.end()) {
    return;  // means, the texture_id is not found.
  }
  const auto camera_stream = TextureId_CameraStream[texture_id];

  result(ErrorOr(PlatformSize(camera_stream->camera_width(),
                                            camera_stream->camera_height())));
  spdlog::debug("[camera_plugin] start the stream for camera_id: {}",
                camera_stream->camera_id());
  camera_stream->Start(camera_stream->camera_id());
}
void CameraPlugin::blit_fb(uint8_t const* pixels) const {
  spdlog::debug("[camera_plugin] blit_fb");
  texture_registrar_->TextureClearCurrent();
  glBindFramebuffer(GL_FRAMEBUFFER, mPreview.framebuffer);
  glViewport(0, 0, mPreview.width, mPreview.height);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, mPreview.textureId);
  glUniform1i(0, 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  // The following call requires a 32-bit aligned source buffer
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, mPreview.width, mPreview.height, 0,
               GL_RGB, GL_UNSIGNED_BYTE, pixels);
  glGenerateMipmap(GL_TEXTURE_2D);

  glBindFramebuffer(GL_FRAMEBUFFER, GL_NONE);
  texture_registrar_->TextureClearCurrent();
  texture_registrar_->MarkTextureFrameAvailable(mPreview.textureId);
}

std::optional<FlutterError> CameraPlugin::Dispose(const int64_t texture_id) {
  spdlog::debug("[camera_plugin] dispose texture_id: {}", texture_id);
  const auto camera_stream = TextureId_CameraStream[texture_id];
  camera_stream->Stop();
  return {};
}

void CameraPlugin::TakePicture(
    const int64_t texture_id,
    const std::function<void(ErrorOr<std::string> reply)> result) {
  spdlog::debug("[camera_plugin] take picture for texture_id: {}", texture_id);
  const auto camera_stream = TextureId_CameraStream[texture_id];
  result(ErrorOr(camera_stream->takePicture()));
}

void CameraPlugin::StartVideoRecording(
    const int64_t /*camera_id*/,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  result({});
}

void CameraPlugin::StopVideoRecording(
    const int64_t /*camera_id*/,
    const std::function<void(ErrorOr<std::string> reply)> /*result*/) {}

void CameraPlugin::PausePreview(
    const int64_t texture_id,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  spdlog::debug("[camera_plugin] pause preview texture_id: {}", texture_id);
  const auto camera_stream = TextureId_CameraStream[texture_id];
  camera_stream->PauseStream();
  result({});
}

void CameraPlugin::ResumePreview(
    const int64_t texture_id,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  spdlog::debug("[camera_plugin] resume preview");
  const auto camera_stream = TextureId_CameraStream[texture_id];
  camera_stream->ResumeStream();
  result({});
}

// ===================== Image-stream helpers =====================

// Optional: tiny 2 fps gray-frame generator to prove the channel works.
// Remove this once PipeWire calls SendI420Frame from its callback.
static gboolean _fake_tick(gpointer self) {
  auto* plugin = static_cast<CameraPlugin*>(self);
/*
  const int width = 640, height = 480;
  const int y_stride = width;
  const int uv_stride = width / 2;
  const size_t y_size = y_stride * height;
  const size_t u_size = uv_stride * (height / 2);
  const size_t v_size = uv_stride * (height / 2);
  std::vector<uint8_t> y(y_size, 128), u(u_size, 128), v(v_size, 128);
  */
  //plugin->SendI420FrameFromJpeg("/home/tcna/Pictures/PhotoCapture_2025_0404_155719_278.jpeg");
  auto stream_ptr = plugin->GetCameraStream(3);
  if (stream_ptr) {
    plugin->SendI420FrameFromCameraStream(*stream_ptr);
  }
  return TRUE; // keep timer
}

// BT.601 limited-range conversion, clamped to [0,255]
static inline uint8_t clamp_to_u8(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return static_cast<uint8_t>(v);
}

// Convert interleaved RGB24 to I420.
// width,height must be even. y/u/v already sized: y=width*height, u=v=(width/2)*(height/2)
static void RGB24ToI420(const uint8_t* rgb, int rgb_stride,
                        uint8_t* y, int y_stride,
                        uint8_t* u, int u_stride,
                        uint8_t* v, int v_stride,
                        int width, int height) {
  // Process 2x2 blocks to subsample U/V
  for (int j = 0; j < height; j += 2) {
    const uint8_t* row0 = rgb + j * rgb_stride;
    const uint8_t* row1 = rgb + (j + 1) * rgb_stride;
    uint8_t* y0 = y + j * y_stride;
    uint8_t* y1 = y + (j + 1) * y_stride;
    uint8_t* urow = u + (j / 2) * u_stride;
    uint8_t* vrow = v + (j / 2) * v_stride;

    for (int i = 0; i < width; i += 2) {
      // Top-left
      int r00 = row0[3 * i + 0];
      int g00 = row0[3 * i + 1];
      int b00 = row0[3 * i + 2];
      // Top-right
      int r01 = row0[3 * (i + 1) + 0];
      int g01 = row0[3 * (i + 1) + 1];
      int b01 = row0[3 * (i + 1) + 2];
      // Bottom-left
      int r10 = row1[3 * i + 0];
      int g10 = row1[3 * i + 1];
      int b10 = row1[3 * i + 2];
      // Bottom-right
      int r11 = row1[3 * (i + 1) + 0];
      int g11 = row1[3 * (i + 1) + 1];
      int b11 = row1[3 * (i + 1) + 2];

      auto Yfun = [](int R, int G, int B) {
        // BT.601 limited range: Y = 16 + 0.257R + 0.504G + 0.098B
        int y = 16 + ((66*R + 129*G + 25*B + 128) >> 8);
        return clamp_to_u8(y);
      };
      auto Ufun = [](int R, int G, int B) {
        // U = 128 - 0.148R - 0.291G + 0.439B
        int u = 128 + ((-38*R - 74*G + 112*B + 128) >> 8);
        return clamp_to_u8(u);
      };
      auto Vfun = [](int R, int G, int B) {
        // V = 128 + 0.439R - 0.368G - 0.071B
        int v = 128 + ((112*R - 94*G - 18*B + 128) >> 8);
        return clamp_to_u8(v);
      };

      // Write Ys
      y0[i + 0] = Yfun(r00,g00,b00);
      y0[i + 1] = Yfun(r01,g01,b01);
      y1[i + 0] = Yfun(r10,g10,b10);
      y1[i + 1] = Yfun(r11,g11,b11);

      // Average U/V over the 2x2 block
      int U00 = Ufun(r00,g00,b00), V00 = Vfun(r00,g00,b00);
      int U01 = Ufun(r01,g01,b01), V01 = Vfun(r01,g01,b01);
      int U10 = Ufun(r10,g10,b10), V10 = Vfun(r10,g10,b10);
      int U11 = Ufun(r11,g11,b11), V11 = Vfun(r11,g11,b11);

      urow[i/2] = clamp_to_u8((U00 + U01 + U10 + U11) / 4);
      vrow[i/2] = clamp_to_u8((V00 + V01 + V10 + V11) / 4);
    }
  }
}

// Decode a JPEG file to RGB24. Returns width, height, data (row-major, 3 bytes/pixel).
static bool DecodeJpegRGB(const std::string& path,
                          int* out_w, int* out_h,
                          std::vector<uint8_t>* out_rgb,
                          int* out_stride) {
  FILE* fp = std::fopen(path.c_str(), "rb");
  if (!fp) return false;

  jpeg_decompress_struct cinfo;
  jpeg_error_mgr jerr;
  cinfo.err = jpeg_std_error(&jerr);

  jpeg_create_decompress(&cinfo);
  jpeg_stdio_src(&cinfo, fp);

  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
    jpeg_destroy_decompress(&cinfo);
    std::fclose(fp);
    return false;
  }

  cinfo.out_color_space = JCS_RGB;  // RGB24
  if (!jpeg_start_decompress(&cinfo)) {
    jpeg_destroy_decompress(&cinfo);
    std::fclose(fp);
    return false;
  }

  int w = static_cast<int>(cinfo.output_width);
  int h = static_cast<int>(cinfo.output_height);

  // Ensure even dims for I420; if odd, we’ll crop the last row/col here.
  if (w % 2) --w;
  if (h % 2) --h;

  const int stride = w * 3;
  out_rgb->assign(static_cast<size_t>(stride) * h, 0);

  std::vector<uint8_t> scanline(static_cast<size_t>(cinfo.output_width) * 3);
  JSAMPROW rowp[1] = { scanline.data() };

  for (int row = 0; row < h; ++row) {
    jpeg_read_scanlines(&cinfo, rowp, 1);
    // Crop if the decoded width is larger than our even width `w`.
    std::memcpy(out_rgb->data() + static_cast<size_t>(row) * stride,
                scanline.data(), static_cast<size_t>(w) * 3);
  }

  // Drain remaining scanlines if the image was taller (odd) than h.
  while (cinfo.output_scanline < cinfo.output_height) {
    jpeg_read_scanlines(&cinfo, rowp, 1);
  }

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  std::fclose(fp);

  *out_w = w;
  *out_h = h;
  *out_stride = stride;
  return true;
}

#include <cstdio>
#include <jpeglib.h>
#include <stdexcept>
#include <string>

// rgb must be RGB24 (3 bytes/pixel), row-major
static bool SaveRGBAsJPEG(uint8_t* rgb,   // <-- note: non-const
                          int w,
                          int h,
                          int rgb_stride,
                          const std::string& filename,
                          int quality = 90) {
  FILE* f = std::fopen(filename.c_str(), "wb");
  if (!f) return false;

  jpeg_compress_struct cinfo;
  jpeg_error_mgr jerr;
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  jpeg_stdio_dest(&cinfo, f);

  cinfo.image_width = w;
  cinfo.image_height = h;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, quality, TRUE);
  jpeg_start_compress(&cinfo, TRUE);

  JSAMPROW row_pointer[1];
  while (cinfo.next_scanline < cinfo.image_height) {
    const size_t offset = static_cast<size_t>(cinfo.next_scanline) * rgb_stride;
    row_pointer[0] = reinterpret_cast<JSAMPROW>(rgb + offset); // no const cast
    jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }

  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  std::fclose(f);
  return true;
}


bool CameraPlugin::SendI420FrameFromCameraStream(CameraStream& stream) {
  //int w = stream.width();
  //int h = stream.height()
  int w = 640;
  int h = 480;

  int rgb_stride = w * 3;

  std::vector<uint8_t> rgb = stream.requestBuffer();
  if (rgb.size() != static_cast<size_t>(rgb_stride) * h) {
    // LOG: failed to get valid buffer
    return false;
  }

  (void)SaveRGBAsJPEG(rgb.data(), w, h, rgb_stride, "/home/tcna/dev/frame.jpg", 90);

  const int y_stride  = w;
  const int uv_stride = w / 2;

  std::vector<uint8_t> y(static_cast<size_t>(y_stride) * h);
  std::vector<uint8_t> u(static_cast<size_t>(uv_stride) * (h / 2));
  std::vector<uint8_t> v(static_cast<size_t>(uv_stride) * (h / 2));

  RGB24ToI420(rgb.data(), rgb_stride,
              y.data(), y_stride,
              u.data(), uv_stride,
              v.data(), uv_stride,
              w, h);

  SendI420Frame(y.data(), y_stride, u.data(), uv_stride, v.data(), uv_stride, w, h);
  return true;
}

bool CameraPlugin::SendI420FrameFromJpeg(const std::string& jpeg_path) {
  int w = 0, h = 0, rgb_stride = 0;
  std::vector<uint8_t> rgb;
  if (!DecodeJpegRGB(jpeg_path, &w, &h, &rgb, &rgb_stride)) {
    // LOG: failed to load/parse jpeg
    return false;
  }

  const int y_stride  = w;
  const int uv_stride = w / 2;

  std::vector<uint8_t> y(static_cast<size_t>(y_stride) * h);
  std::vector<uint8_t> u(static_cast<size_t>(uv_stride) * (h / 2));
  std::vector<uint8_t> v(static_cast<size_t>(uv_stride) * (h / 2));

  RGB24ToI420(rgb.data(), rgb_stride,
              y.data(), y_stride,
              u.data(), uv_stride,
              v.data(), uv_stride,
              w, h);

  // Reuse your existing sender
  SendI420Frame(y.data(), y_stride, u.data(), uv_stride, v.data(), uv_stride, w, h);
  return true;
}

void CameraPlugin::StartImageStream() {
  // For now start the fake timer; replace with PipeWire stream start.
  static guint timer_id = 0;
  if (timer_id == 0) {
    timer_id = g_timeout_add(50, _fake_tick, this); // 2 fps
  }
}

void CameraPlugin::StopImageStream() {
  // Stop the fake timer; replace with PipeWire stop.
  extern guint g_main_current_source(); // (not needed; we'll track our own id)
  // If you saved timer_id in a member, remove it here. This stub keeps it static in StartImageStream.
}

void CameraPlugin::SendI420Frame(const uint8_t* y, int y_stride,
                                 const uint8_t* u, int u_stride,
                                 const uint8_t* v, int v_stride,
                                 int width, int height) {
  if (!image_sink_) return;

  using flutter::EncodableList;
  using flutter::EncodableMap;
  using flutter::EncodableValue;

  // Copy into message-owned vectors (simple & safe for first pass).
  std::vector<uint8_t> yv(y, y + y_stride * height);
  std::vector<uint8_t> uv(u, u + u_stride * (height / 2));
  std::vector<uint8_t> vv(v, v + v_stride * (height / 2));

  EncodableList planes;
  planes.emplace_back(EncodableMap{
      {EncodableValue("bytes"),       EncodableValue(std::move(yv))},
      {EncodableValue("bytesPerRow"), EncodableValue(y_stride)},
      {EncodableValue("bytesPerPixel"), EncodableValue(1)},          // <-- add this

  });
  planes.emplace_back(EncodableMap{
      {EncodableValue("bytes"),       EncodableValue(std::move(uv))},
      {EncodableValue("bytesPerRow"), EncodableValue(u_stride)},
      {EncodableValue("bytesPerPixel"), EncodableValue(1)},          // <-- add this

  });
  planes.emplace_back(EncodableMap{
      {EncodableValue("bytes"),       EncodableValue(std::move(vv))},
      {EncodableValue("bytesPerRow"), EncodableValue(v_stride)},
      {EncodableValue("bytesPerPixel"), EncodableValue(1)},          // <-- add this

  });

  EncodableMap event{
      {EncodableValue("width"),       EncodableValue(width)},
      {EncodableValue("height"),      EncodableValue(height)},
      {EncodableValue("formatGroup"), EncodableValue("yuv421")},
      {EncodableValue("raw"),         EncodableValue("I420")},
      {EncodableValue("planes"),      EncodableValue(std::move(planes))},
  };

  image_sink_->Success(flutter::EncodableValue(std::move(event)));
}

void CameraPlugin::SendNV12Frame(const uint8_t* y, int y_stride,
                                 const uint8_t* uv, int uv_stride,
                                 int width, int height) {
  if (!image_sink_) return;
  using flutter::EncodableList; using flutter::EncodableMap; using flutter::EncodableValue;

  std::vector<uint8_t> yv(y, y + y_stride * height);
  std::vector<uint8_t> uvv(uv, uv + uv_stride * (height / 2));

  EncodableList planes;
  planes.emplace_back(EncodableMap{
    {EncodableValue("bytes"),       EncodableValue(std::move(yv))},
    {EncodableValue("bytesPerRow"), EncodableValue(y_stride)},
    {EncodableValue("bytesPerPixel"), EncodableValue(1)},  // Y samples are 1 byte

  });
  planes.emplace_back(EncodableMap{
    {EncodableValue("bytes"),       EncodableValue(std::move(uvv))},
    {EncodableValue("bytesPerRow"), EncodableValue(uv_stride)},
    {EncodableValue("bytesPerPixel"), EncodableValue(2)},  // interleaved UV -> pixel stride 2

  });

  EncodableMap event{
    {EncodableValue("width"),       EncodableValue(width)},
    {EncodableValue("height"),      EncodableValue(height)},
    {EncodableValue("formatGroup"), EncodableValue("yuv420")},
    {EncodableValue("raw"),         EncodableValue("NV12")},
    {EncodableValue("planes"),      EncodableValue(std::move(planes))},
  };
  image_sink_->Success(flutter::EncodableValue(std::move(event)));
}



void CameraPlugin::SendJpegFrame(const std::string& jpeg_path) {
    if (!image_sink_) return;
  using flutter::EncodableList; using flutter::EncodableMap; using flutter::EncodableValue;

  // ---- 1) Probe dimensions with libjpeg (no full decode) ----
  int width = 0, height = 0;
  {
    FILE* fp = std::fopen(jpeg_path.c_str(), "rb");
    if (!fp) {
      // Optional: report to Dart
      // image_sink_->Error("io", "Failed to open JPEG: " + jpeg_path);
      return;
    }
    jpeg_decompress_struct cinfo;
    jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    const int hdr_ok = jpeg_read_header(&cinfo, TRUE);
    if (hdr_ok == JPEG_HEADER_OK) {
      width  = static_cast<int>(cinfo.image_width);
      height = static_cast<int>(cinfo.image_height);
    }
    jpeg_destroy_decompress(&cinfo);
    std::fclose(fp);

    if (width <= 0 || height <= 0) {
      // image_sink_->Error("format", "Invalid JPEG header: " + jpeg_path);
      return;
    }
  }

  // ---- 2) Read the compressed JPEG bytes ----
  std::vector<uint8_t> jpeg_bytes;
  {
    std::ifstream in(jpeg_path, std::ios::binary | std::ios::ate);
    if (!in) {
      // image_sink_->Error("io", "Failed to read JPEG: " + jpeg_path);
      return;
    }
    const std::streamsize size = in.tellg();
    if (size <= 0) {
      // image_sink_->Error("io", "Empty JPEG: " + jpeg_path);
      return;
    }
    in.seekg(0, std::ios::beg);
    jpeg_bytes.resize(static_cast<size_t>(size));
    if (!in.read(reinterpret_cast<char*>(jpeg_bytes.data()), size)) {
      // image_sink_->Error("io", "Short read for JPEG: " + jpeg_path);
      return;
    }
  }

  // ---- 3) Package one-plane JPEG payload and send ----
  EncodableList planes;
  planes.emplace_back(EncodableMap{
      {EncodableValue("bytes"),         EncodableValue(std::move(jpeg_bytes))},
      // Stride & bpp are not meaningful for compressed data; send 0.
      {EncodableValue("bytesPerRow"),   EncodableValue(0)},
      {EncodableValue("bytesPerPixel"), EncodableValue(0)},
  });

  EncodableMap event{
      {EncodableValue("width"),       EncodableValue(width)},
      {EncodableValue("height"),      EncodableValue(height)},
      {EncodableValue("formatGroup"), EncodableValue("jpeg")},  // <—
      {EncodableValue("raw"),         EncodableValue("JPEG")},  // <—
      {EncodableValue("planes"),      EncodableValue(std::move(planes))},
  };

  image_sink_->Success(EncodableValue(std::move(event)));
}
}  // namespace camera_plugin
