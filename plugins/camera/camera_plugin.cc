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

#include <memory>
#include <unordered_map>

#include <libcamera/libcamera.h>

#include "camera_session.h"

#include "plugins/common/common.h"

#include <pipewire/pipewire.h>
#include <pipewire/core.h>
#include <pipewire/properties.h>
#include <spa/param/param.h>
#include <spa/param/video/format-utils.h>
#include <jpeglib.h>
#include <iostream>
#include <vector>
#include <string>
#include <SDL2/SDL.h>
#include "camera_context.h"

extern "C" {
#include <pipewire/pipewire.h>
}

struct CameraInfo {
  uint32_t id;
  std::string name;
};

// Global vector to store camera info
std::vector<CameraInfo> cameras;

// For streaming
static constexpr int WIDTH  = 640;
static constexpr int HEIGHT = 480;

// Callback function for detecting cameras
void on_global(void *data, uint32_t id, uint32_t permissions,
               const char *type, uint32_t version, const struct spa_dict *props) {
  if (!props) return;

  const char *media_class = spa_dict_lookup(props, "media.class");
  const char *name = spa_dict_lookup(props, "node.description");

  if (media_class && std::string(media_class) == "Video/Source") {
    std::cout << "Found camera: " << (name ? name : "Unknown") << " (id: " << id << ")" << std::endl;
    cameras.push_back({id,name ? name: "Unknown"});
  }
}

// Function to enumerate cameras using PipeWire
std::vector<CameraInfo> enumerate_cameras() {
  cameras.clear();  // Clear previous entries

  pw_init(nullptr, nullptr);

  pw_main_loop *loop = pw_main_loop_new(nullptr);
  pw_context *context = pw_context_new(pw_main_loop_get_loop(loop), nullptr, 0);
  pw_core *core = pw_context_connect(context, nullptr, 0);
  pw_registry *registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);

  spa_hook registry_listener;
  static const pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = on_global,
};

  pw_registry_add_listener(registry, &registry_listener, &registry_events, nullptr);

  // Run the main loop briefly to gather device info
  //pw_main_loop_run(loop);
  int timeout_ms = 1000;
  int elapsed_ms = 0;
  while (elapsed_ms < timeout_ms) {
    pw_loop_iterate(pw_main_loop_get_loop(loop), 100);
    elapsed_ms += 100;
  }
  // Cleanup
  spa_hook_remove(&registry_listener);
  pw_proxy_destroy(reinterpret_cast<pw_proxy *>(registry));
  pw_core_disconnect(core);
  pw_context_destroy(context);
  pw_main_loop_destroy(loop);

  return cameras;
}


using namespace plugin_common;

namespace camera_plugin {

// TODO static constexpr char kKeyMaxVideoDuration[] = "maxVideoDuration";

// TODO static constexpr char kResolutionPresetValueLow[] = "low";
// TODO static constexpr char kResolutionPresetValueMedium[] = "medium";
// TODO static constexpr char kResolutionPresetValueHigh[] = "high";
// TODO static constexpr char kResolutionPresetValueVeryHigh[] = "veryHigh";
// TODO static constexpr char kResolutionPresetValueUltraHigh[] = "ultraHigh";
// TODO static constexpr char kResolutionPresetValueMax[] = "max";

static std::unique_ptr<libcamera::CameraManager> g_camera_manager;
//static std::vector<std::shared_ptr<CameraContext>> g_cameras;
static std::unordered_map<unsigned int, std::shared_ptr<CameraSession>>
    g_camera_sessions;

// static
void CameraPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarDesktop* registrar) {
  auto plugin =
      std::make_unique<CameraPlugin>(registrar, registrar->messenger());
  CameraPlugin::SetUp(registrar->messenger(), plugin.get());
  registrar->AddPlugin(std::move(plugin));
}

CameraPlugin::CameraPlugin(flutter::PluginRegistrarDesktop* plugin_registrar,
                           flutter::BinaryMessenger* messenger)
    : registrar_(plugin_registrar),
      messenger_(messenger),
      io_context_(std::make_unique<asio::io_context>(ASIO_CONCURRENCY_HINT_1)),
      work_(io_context_->get_executor()),
      strand_(std::make_unique<asio::io_context::strand>(*io_context_)) {
  /*
  thread_ = std::thread([&]() { io_context_->run(); });
  g_camera_manager = std::make_unique<libcamera::CameraManager>();
  g_camera_manager->cameraAdded.connect(this, &CameraPlugin::camera_added);
  g_camera_manager->cameraRemoved.connect(this, &CameraPlugin::camera_removed);

  spdlog::debug("[camera_plugin] libcamera {}", g_camera_manager->version());

  auto res = g_camera_manager->start();
  if (res != 0) {
    spdlog::critical("Failed to start camera manager: {}", strerror(-res));
  }
  */
}

CameraPlugin::~CameraPlugin() {
  io_context_->run();
  thread_.join();

  g_camera_manager->stop();
  for (auto& [texture_id, camera] : g_camera_sessions) {
    camera.reset();
  }
}

void CameraPlugin::camera_added(const std::shared_ptr<libcamera::Camera>& cam) {
  spdlog::debug("[camera_plugin] Camera added: {}", cam->id());
}

void CameraPlugin::camera_removed(
    const std::shared_ptr<libcamera::Camera>& cam) {
  spdlog::debug("[camera_plugin] Camera removed: {}", cam->id());
  for (const auto& [texture_id, camera] : g_camera_sessions) {
    if (camera->get_libcamera_id() == cam->id()) {
      switch (camera->get_camera_state()) {
        case CameraSession::CAM_STATE_RUNNING:
          cam->stop();
        case CameraSession::CAM_STATE_ACQUIRED:
        case CameraSession::CAM_STATE_CONFIGURED:
          cam->release();
          break;
        default:
          break;
      }
    }
  }
}

std::string CameraPlugin::get_camera_lens_facing(
    const std::shared_ptr<libcamera::Camera>& camera) {
  const libcamera::ControlList& props = camera->properties();
  std::string lensFacing;

  // If location is specified use it, otherwise select external
  if (const auto& location = props.get(libcamera::properties::Location)) {
    switch (*location) {
      case libcamera::properties::CameraLocationFront:
        lensFacing = "front";
        break;
      case libcamera::properties::CameraLocationBack:
        lensFacing = "back";
        break;
      case libcamera::properties::CameraLocationExternal:
        lensFacing = "external";
        break;
      default:;
    }
  } else {
    lensFacing = "external";
  }
  return std::move(lensFacing);
}

ErrorOr<flutter::EncodableList> CameraPlugin::GetAvailableCameras() {
  std::vector<CameraInfo> pwcameras = enumerate_cameras();
  spdlog::debug("[camera_plugin] availableCameras:");
  flutter::EncodableList list;
  for (auto& camera : pwcameras) {
    std::string id = std::to_string(camera.id);
    std::string name = camera.name;

    spdlog::debug("\tid: {}", id);
    spdlog::debug("\tname: {}", name);
    list.emplace_back(flutter::EncodableValue(std::move(id)));
  }
  return list;
}

void CameraPlugin::Create(
    const std::string& camera_name,
    const PlatformMediaSettings& settings,
    const std::function<void(ErrorOr<int64_t> reply)> result) {

  spdlog::debug("[camera_plugin] create:");

  spdlog::debug("\tname: {}", camera_name);

  if(CameraName_CameraStream.find(camera_name)==CameraName_CameraStream.end()) {
    //The camera is not created before
    //CameraStream newCamera = CameraStream(registrar_, 640,480 );
    //CameraName_CameraStream.insert({camera_name, newCamera});
    auto new_camera = std::make_shared<CameraStream>(registrar_, camera_name, 640, 480);
    CameraName_CameraStream.insert({camera_name, new_camera});
    TextureId_CameraStream.insert({new_camera->texture_id(), new_camera});
    //result(new_camera->get_textureId());
  }
  std::cout << "textureID of "<< camera_name <<" is : "<< CameraName_CameraStream[camera_name]->texture_id()<<std::endl;
  result(CameraName_CameraStream[camera_name]->texture_id());

}
/******************************************************************************
 * decode_mjpeg
 ******************************************************************************/
int decode_mjpeg(const uint8_t *input, size_t input_size,
                 uint8_t *output, int out_width, int out_height)
{
  jpeg_decompress_struct cinfo;
  jpeg_error_mgr jerr;

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);

  jpeg_mem_src(&cinfo, input, input_size);
  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
    std::cerr << "[decode_mjpeg] Failed to read JPEG header.\n";
    jpeg_destroy_decompress(&cinfo);
    return -1;
  }

  jpeg_start_decompress(&cinfo);
  if (cinfo.output_width  != static_cast<uint32_t>(out_width)  ||
      cinfo.output_height != static_cast<uint32_t>(out_height) ||
      cinfo.output_components != 3)
  {
    std::cerr << "[decode_mjpeg] Unexpected size/components.\n";
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return -1;
  }

  const int row_stride = cinfo.output_width * cinfo.output_components;
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
static void parse_props_param(const spa_pod *pod)
{
    if (!pod) return;

    // Is this actually an object of type SPA_TYPE_OBJECT_Props?
    if (!spa_pod_is_object_type(pod, SPA_TYPE_OBJECT_Props)) {
        // Some cameras or older nodes might still pass different param objects
        return;
    }

    const spa_pod_object *obj = reinterpret_cast<const spa_pod_object *>(pod);
    std::cout << "[parse_props_param] Found a props object with the following items:\n";

    // Iterate each property (key/value)
    spa_pod_prop *prop;
    SPA_POD_OBJECT_FOREACH(obj, prop) {
        uint32_t key = prop->key;

        // We can check some known keys from <spa/param/props.h>:
        // e.g. SPA_PROP_brightness, SPA_PROP_contrast, etc.
        // We'll just print the key numeric ID and try to parse as float/int, etc.
        std::cout << "  Key=" << key << " => ";

        // The prop->value is a spa_pod describing the property type
        if (SPA_POD_TYPE(&prop->value) == SPA_TYPE_Float) {
            float val = 0.0f;
            spa_pod_get_float(&prop->value, &val);
            std::cout << "float=" << val;
        }
        else if (SPA_POD_TYPE(&prop->value) == SPA_TYPE_Int) {
            int val = 0;
            spa_pod_get_int(&prop->value, &val);
            std::cout << "int=" << val;
        }
        else if (SPA_POD_TYPE(&prop->value) == SPA_TYPE_Bool) {
            bool val = false;
            spa_pod_get_bool(&prop->value, &val);
            std::cout << "bool=" << val;
        }
        else {
            // We won't parse all possible types in this example
            std::cout << "(unknown type=" << SPA_POD_TYPE(&prop->value) << ")";
        }
        std::cout << "\n";
    }
    std::cout << "[parse_props_param] End of props.\n";
}
/******************************************************************************
 * on_stream_process: Called to decode MJPEG frames
 ******************************************************************************/
void CameraPlugin::on_stream_process(void* data)
{
  auto *self = static_cast<CameraPlugin*>(data);
  self->handle_stream();
/*
  pw_buffer *buf = pw_stream_dequeue_buffer(self->g_pwStream);
  if (!buf) return;

  if (!buf->buffer->datas[0].data) {
    pw_stream_queue_buffer(self->g_pwStream, buf);
    return;
  }

  auto *compressedData = static_cast<uint8_t*>(buf->buffer->datas[0].data);
  size_t compressedSize = buf->buffer->datas[0].chunk->size;

  int ret = decode_mjpeg(compressedData, compressedSize, self->g_decodedBuffer.get(), WIDTH, HEIGHT);
  if (ret == 0) {
    std::lock_guard<std::mutex> lock(self->g_frameMutex);
    self->g_newFrameAvailable = true;

  } else {
    std::cerr << "[on_stream_process] MJPEG decode failed.\n";
  }

  pw_stream_queue_buffer(self->g_pwStream, buf);
  */
}

void CameraPlugin::handle_stream() {
  pw_buffer *buf = pw_stream_dequeue_buffer(g_pwStream);
  if (!buf) return;

  if (!buf->buffer->datas[0].data) {
    pw_stream_queue_buffer(g_pwStream, buf);
    return;
  }

  auto *compressedData = static_cast<uint8_t*>(buf->buffer->datas[0].data);
  size_t compressedSize = buf->buffer->datas[0].chunk->size;

  int ret = decode_mjpeg(compressedData, compressedSize, g_decodedBuffer.get(), WIDTH, HEIGHT);
  if (ret == 0) {
    std::lock_guard<std::mutex> lock(g_frameMutex);
    g_newFrameAvailable = true;
    SPDLOG_TRACE("[camera_plugin] Texture::blit_fb");
    registrar_->texture_registrar()->TextureMakeCurrent();
    glBindFramebuffer(GL_FRAMEBUFFER, mPreview.framebuffer);
    glViewport(0, 0, mPreview.width, mPreview.height);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mPreview.textureId);
    glUniform1i(0, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, mPreview.width, mPreview.height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, g_decodedBuffer.get());
    glGenerateMipmap(GL_TEXTURE_2D);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    registrar_->texture_registrar()->TextureClearCurrent();
    registrar_->texture_registrar()->MarkTextureFrameAvailable(mPreview.textureId);
    glFinish();

  } else {
    std::cerr << "[on_stream_process] MJPEG decode failed.\n";
  }

  pw_stream_queue_buffer(g_pwStream, buf);
}

/******************************************************************************
 * on_stream_param_changed: Called when we get new param blocks from the stream
 *
 * We'll check if it's SPA_PARAM_Props or SPA_PARAM_PropInfo, then parse them
 * with parse_props_param().
 ******************************************************************************/
static void on_stream_param_changed(void*, uint32_t id, const spa_pod* param)
{
  if (!param) return;

  switch (id) {
    case SPA_PARAM_Props:
      std::cout << "[on_stream_param_changed] Received SPA_PARAM_Props\n";
    parse_props_param(param);
    break;
    case SPA_PARAM_PropInfo:
      std::cout << "[on_stream_param_changed] Received SPA_PARAM_PropInfo\n";
    // For demonstration, we can parse it similarly
    // or skip if we just want to know it exists
    parse_props_param(param);
    break;
    default:
      std::cout << "[on_stream_param_changed] Received param id=" << id
                << " (not handled)\n";
    break;
  }
}
/******************************************************************************
 * on_stream_state_changed
 ******************************************************************************/
void CameraPlugin::on_stream_state_changed(void *data,
                                    pw_stream_state old_state,
                                    pw_stream_state new_state,
                                    const char* error)
{
  std::cout << "[on_stream_state_changed] "
            << old_state << " -> " << new_state << " ("
            << (error ? error : "no error") << ")\n";
  if (new_state == PW_STREAM_STATE_STREAMING) {
    // Just an example of changing a property
    //update_exposure(g_pwStream, "9000");
  }
}
/******************************************************************************
 * start_camera_stream: Create a PipeWire stream for the chosen camera node,
 * run until pw_main_loop_quit(). We also call print_camera_parameters() here
 * after connect, so we can see the advanced param listing.
 ******************************************************************************/

void CameraPlugin::start_camera_stream(const std::string &nodeID)
{

  if (g_pwLoop) {
    // Already started
    return;
  }
  //pw_main_loop_quit(g_pwLoop);
  if (pipewire_thread_.joinable()) {
    pipewire_thread_.join();
  }
  if(g_pwStream) {
    pw_stream_destroy(g_pwStream);
    g_pwStream=nullptr;
  }
  if (g_pwCore) {
    pw_core_disconnect(g_pwCore);
    g_pwCore = nullptr;
  }
  if (g_pwContext) {
    pw_context_destroy(g_pwContext);
    g_pwContext = nullptr;
  }
  if (g_pwLoop) {
    pw_main_loop_destroy(g_pwLoop);
    g_pwLoop = nullptr;
  }
  pw_deinit();



  pw_init(nullptr, nullptr);

    spdlog::debug("[camera_plugin] nodeID: {}", nodeID);

    g_pwLoop    = pw_main_loop_new(nullptr);
    g_pwContext = pw_context_new(pw_main_loop_get_loop(g_pwLoop), nullptr, 0);
    g_pwCore    = pw_context_connect(g_pwContext, nullptr, 0);
    if (!g_pwCore) {
        std::cerr << "[start_camera_stream] Could not connect to PipeWire core.\n";
        return;
    }

    // Create stream
    pw_properties* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE,         "Video",
        PW_KEY_MEDIA_CATEGORY,     "Capture",
        PW_KEY_MEDIA_ROLE,         "Camera",
        PW_KEY_NODE_PAUSE_ON_IDLE, "false",
        // This is deprecated in modern PipeWire, but older versions still use it:
        PW_KEY_NODE_TARGET,        nodeID.c_str(),
        nullptr
    );
    g_pwStream = pw_stream_new(g_pwCore, "MJPEG Camera Stream", props);
    if (!g_pwStream) {
        std::cerr << "[start_camera_stream] Failed to create pw_stream.\n";
        return;
    }

    static const pw_stream_events streamEvents = {
        PW_VERSION_STREAM_EVENTS,
        nullptr,                  // destroy
        on_stream_state_changed,  // state_changed
        nullptr,                  // control_info
        nullptr,                  // io_changed
        on_stream_param_changed,  // param_changed  <--- we parse them here
        nullptr,                  // add_buffer
        nullptr,                  // remove_buffer
        on_stream_process,        // process
        nullptr                   // drain
    };
    pw_stream_add_listener(g_pwStream, &localListener, &streamEvents, this);

    // Build a SPA format param for MJPEG 640x480@30fps
    uint8_t buffer[1024];
    spa_pod_builder builder  = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_rectangle rect       = { static_cast<uint32_t>(WIDTH), static_cast<uint32_t>(HEIGHT) };
    spa_fraction fps         = { 30, 1 };

    const spa_pod* params[1];
    params[0] = reinterpret_cast<const spa_pod*>(
        spa_pod_builder_add_object(&builder,
            SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
            SPA_FORMAT_mediaType,        SPA_POD_Id(SPA_MEDIA_TYPE_video),
            SPA_FORMAT_mediaSubtype,     SPA_POD_Id(SPA_MEDIA_SUBTYPE_mjpg),
            SPA_FORMAT_VIDEO_size,       SPA_POD_Rectangle(&rect),
            SPA_FORMAT_VIDEO_framerate,  SPA_POD_Fraction(&fps)
        )
    );

    int res = pw_stream_connect(
        g_pwStream,
        PW_DIRECTION_INPUT,
        PW_ID_ANY,
        static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT |
            PW_STREAM_FLAG_MAP_BUFFERS  |
            PW_STREAM_FLAG_RT_PROCESS
        ),
        params,
        1
    );
    if (res < 0) {
        std::cerr << "[start_camera_stream] pw_stream_connect() error: " << res << "\n";
    }

    // -- Immediately request param blocks so we can print them out
    //print_camera_parameters();

    // Run the loop until user calls pw_main_loop_quit()

  pipewire_thread_ = std::thread([this]() {
  pw_main_loop_run(g_pwLoop);
});
}

#define IMAGE_WIDTH 640
#define IMAGE_HEIGHT 480
#define IMAGE_CHANNELS 3  // RGB format

void save_image_to_jpeg(const std::string &filename, const unsigned char *image_data, int width, int height, int channels, int quality) {
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;

  // Setup error handling
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);

  // Open file for writing
  FILE *outfile = fopen(filename.c_str(), "wb");
  if (!outfile) {
    std::cerr << "Error: Unable to open file " << filename << " for writing!" << std::endl;
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
    row_pointer = (JSAMPROW)&image_data[cinfo.next_scanline * width * channels];
    jpeg_write_scanlines(&cinfo, &row_pointer, 1);
  }

  // Finish compression
  jpeg_finish_compress(&cinfo);
  fclose(outfile);
  jpeg_destroy_compress(&cinfo);

  std::cout << "Image saved to " << filename << std::endl;
}

void CameraPlugin::Initialize(
    const int64_t camera_id,
    const std::function<void(ErrorOr<PlatformSize> reply)> result) {

  std::cout<< "CameraPlugin::Initialize: "<< camera_id<<std::endl;
  //GLuint textureID= camera_id;
  if(TextureId_CameraStream.find(camera_id)==TextureId_CameraStream.end()) {
    return;//means, the camera_id is not found.
  }
  auto camera_stream= TextureId_CameraStream[camera_id];
/*
  for ( std::map<std::string, std::shared_ptr<CameraStream>>::iterator it= CameraName_CameraStream.begin(); it != CameraName_CameraStream.end(); ++it) {
    std::cout << "camera name: "<< it->second->camera_name()<<std::endl;
    if(it->second->texture_id()== camera_id) {
      std::cout<< "found the texture_id: " << it->second->texture_id()<<std::endl;
      std::cout<< "its camera name: "<< it->second->camera_name();
      break;
    }
  }
  */
  //auto cameraStream = CameraName_CameraStream
  result(PlatformSize(camera_stream->camera_width(), camera_stream->camera_height()));
  //std::string nodeID=camera_stream->camera_name();


  camera_stream->Start(camera_stream->camera_name());
}
void CameraPlugin::blit_fb(uint8_t const* pixels) const {
  SPDLOG_TRACE("[camera_plugin] Texture::blit_fb");
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
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, mPreview.width, mPreview.height, 0, GL_RGB,
               GL_UNSIGNED_BYTE, pixels);
  glGenerateMipmap(GL_TEXTURE_2D);

  glBindFramebuffer(GL_FRAMEBUFFER, GL_NONE);
  texture_registrar_->TextureClearCurrent();
  texture_registrar_->MarkTextureFrameAvailable(mPreview.textureId);


  //glBindFramebuffer(GL_FRAMEBUFFER, mPreview.framebuffer);
  //texture_registrar->MarkTextureFrameAvailable(mPreview.textureId);

}

std::optional<FlutterError> CameraPlugin::Dispose(const int64_t camera_id) {
  //auto camera = g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  //camera.reset();
  SPDLOG_DEBUG("[camera_plugin] dispose: {}", camera_id);
  auto camera_stream= TextureId_CameraStream[camera_id];
  camera_stream->Stop();
  return {};
}

void CameraPlugin::TakePicture(
    const int64_t camera_id,
    const std::function<void(ErrorOr<std::string> reply)> result) {

  //const auto camera =
  //    g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  const auto camera = g_camera_sessions[camera_id];
  //camera->pausePreview();
/*
  SPDLOG_DEBUG("[camera_plugin] pause the camera: {}");

  libcamera::StreamRole stream_roles = { libcamera::StreamRole::StillCapture };
  //std::unique_ptr<libcamera::CameraConfiguration> config =
    //camera->generateConfiguration(stream_roles);
  //camera->
  camera->resumePreview();
*/
  result(camera->takePicture());
}

void CameraPlugin::StartVideoRecording(
    const int64_t camera_id,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  bool enable_stream{};

  const auto camera =
      g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  camera->startVideoRecording(enable_stream);

  result({});
}

void CameraPlugin::StopVideoRecording(
    const int64_t camera_id,
    const std::function<void(ErrorOr<std::string> reply)> result) {
  const auto camera =
      g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  result(camera->stopVideoRecording());
}

void CameraPlugin::PausePreview(
    const int64_t camera_id,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  //const auto camera =
  //    g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  const auto camera = g_camera_sessions[camera_id];
  if(camera) {
    SPDLOG_DEBUG("[camera_plugin] texture_id: {}", camera->get_texture_id());
    camera->pausePreview();
  }
  else
    SPDLOG_DEBUG("[camera_plugin] no camera session was found!!");

  result({});
}

void CameraPlugin::ResumePreview(
    const int64_t camera_id,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  //const auto camera =
  //g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  const auto camera = g_camera_sessions[camera_id];
  camera->resumePreview();
  result({});
}
}  // namespace camera_plugin
