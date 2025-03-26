//
// Created by tcna on 3/24/25.
//

#include "CameraStream.h"

#include <GLES2/gl2.h>
#include <jpeglib.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <iostream>

//------------------------------------------------------------------------------
// A helper function for MJPEG decoding
//------------------------------------------------------------------------------
static int decode_mjpeg(const uint8_t *input, size_t input_size,
                        uint8_t *output, int out_width, int out_height)
{
  jpeg_decompress_struct cinfo;
  jpeg_error_mgr jerr;
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);

  jpeg_mem_src(&cinfo, input, input_size);
  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
    std::fprintf(stderr, "[decode_mjpeg] Failed to read JPEG header.\n");
    jpeg_destroy_decompress(&cinfo);
    return -1;
  }

  jpeg_start_decompress(&cinfo);
  if ((int)cinfo.output_width  != out_width ||
      (int)cinfo.output_height != out_height ||
      cinfo.output_components  != 3) {
    std::fprintf(stderr, "[decode_mjpeg] Unexpected size.\n");
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

//------------------------------------------------------------------------------
// Constructor
//------------------------------------------------------------------------------
CameraStream::CameraStream(flutter::PluginRegistrarDesktop* plugin_registrar,
                           std::string camera_name, int width, int height)
    :  registrar_(plugin_registrar),
      camera_name_(camera_name),
      width_(width),
      height_(height)
{
  // Allocate RGB buffer for frames
  decoded_buffer_.reset(new uint8_t[width_ * height_ * 3]);
  std::memset(decoded_buffer_.get(), 0, width_ * height_ * 3);

  // Create the Flutter PixelBufferTexture

  auto pixel_buffer_texture =
      std::make_unique<flutter::PixelBufferTexture>(
          [this](size_t /*width*/, size_t /*height*/) -> const FlutterDesktopPixelBuffer* {
            static FlutterDesktopPixelBuffer pixel_buffer = {};
            static std::mutex s_mutex;
            std::lock_guard<std::mutex> lock(s_mutex);

            pixel_buffer.width  = width_;
            pixel_buffer.height = height_;
            pixel_buffer.buffer = decoded_buffer_.get();

            // No custom release callback
            pixel_buffer.release_context  = nullptr;
            pixel_buffer.release_callback = nullptr;
            return &pixel_buffer;
          });

  registrar_->texture_registrar()->TextureMakeCurrent();

  glGenFramebuffers(1, &framebuffer_);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);

  glGenTextures(1, &texture_id_);
  glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glBindTexture(GL_TEXTURE_2D, texture_id_);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glBindTexture(GL_TEXTURE_2D, 0);


  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture_id_, 0);

  if (auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
      status != GL_FRAMEBUFFER_COMPLETE) {
    spdlog::error("[camera_plugin] FramebufferStatus: 0x{:X}", status);
      }

  glFinish();
  glBindFramebuffer(GL_FRAMEBUFFER, 0);


  registrar_->texture_registrar()->TextureClearCurrent();

  descriptor = {
    .struct_size = sizeof(FlutterDesktopGpuSurfaceDescriptor),
    .handle = &texture_id_,
    .width = static_cast<size_t>(width),
    .height = static_cast<size_t>(height),
    .visible_width = static_cast<size_t>(width),
    .visible_height = static_cast<size_t>(height),
    .format = kFlutterDesktopPixelFormatRGBA8888,
    .release_callback = [](void* /* release_context */) {},
    .release_context = this,
  };

  gpu_surface_texture = std::make_unique<flutter::GpuSurfaceTexture>(
    kFlutterDesktopGpuSurfaceTypeGlTexture2D,
    [&](size_t /* width */,
        size_t /* height */) -> const FlutterDesktopGpuSurfaceDescriptor* {
      return &descriptor;
    });

  flutter::TextureVariant texture = *gpu_surface_texture;
  registrar_->texture_registrar()->RegisterTexture(&texture);
  registrar_->texture_registrar()->MarkTextureFrameAvailable(texture_id_);

}

//------------------------------------------------------------------------------
// Destructor
//------------------------------------------------------------------------------
CameraStream::~CameraStream()
{
  Stop();
/*
  // Optionally unregister the texture if you want; often unnecessary
  if (texture_registrar_ && texture_id_ != -1) {
    texture_registrar_->UnregisterTexture(texture_id_);
    texture_id_ = -1;
  }
  */
}

//------------------------------------------------------------------------------
// Start capturing from the given node ID
//------------------------------------------------------------------------------
bool CameraStream::Start(const std::string &nodeID)
{
  if (pw_loop_) {
    // Already started
    return true;
  }

  pw_init(nullptr, nullptr);

  pw_loop_    = pw_main_loop_new(nullptr);
  pw_context_ = pw_context_new(pw_main_loop_get_loop(pw_loop_), nullptr, 0);
  pw_core_    = pw_context_connect(pw_context_, nullptr, 0);
  if (!pw_core_) {
    std::fprintf(stderr, "[CameraStream::Start] Could not connect to PW core.\n");
    return false;
  }

  // Create the stream
  pw_properties* props = pw_properties_new(
      PW_KEY_MEDIA_TYPE,         "Video",
      PW_KEY_MEDIA_CATEGORY,     "Capture",
      PW_KEY_MEDIA_ROLE,         "Camera",
      PW_KEY_NODE_PAUSE_ON_IDLE, "false",
      PW_KEY_NODE_TARGET,        nodeID.c_str(),
      nullptr
  );
  pw_stream_ = pw_stream_new(pw_core_, "MJPEG Camera Stream", props);
  if (!pw_stream_) {
    std::fprintf(stderr, "[CameraStream::Start] Failed to create pw_stream.\n");
    return false;
  }

  // Set up callbacks
  static pw_stream_events streamEvents = {
    PW_VERSION_STREAM_EVENTS,
    nullptr,                  // destroy
    OnStreamStateChanged,     // state_changed
    nullptr,                  // control_info
    nullptr,                  // io_changed
    OnStreamParamChanged,     // param_changed
    nullptr,                  // add_buffer
    nullptr,                  // remove_buffer
    OnStreamProcess,          // process
    nullptr                   // drain
  };
  pw_stream_add_listener(pw_stream_, &stream_listener_, &streamEvents, this);

  // Build the SPA format param for MJPEG @ 640x480@30
  uint8_t buffer[1024];
  spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  spa_rectangle rect      = { (uint32_t)width_, (uint32_t)height_ };
  spa_fraction fps        = { 30, 1 };

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
      pw_stream_,
      PW_DIRECTION_INPUT,
      PW_ID_ANY,
      (pw_stream_flags)(
          PW_STREAM_FLAG_AUTOCONNECT |
          PW_STREAM_FLAG_MAP_BUFFERS |
          PW_STREAM_FLAG_RT_PROCESS
      ),
      params, 1
  );

  if (res < 0) {
    std::fprintf(stderr, "[CameraStream::Start] pw_stream_connect() error: %d\n", res);
    return false;
  }

  // Launch the PipeWire main loop in a separate thread
  pipewire_thread_ = std::thread(&CameraStream::RunPipeWireLoop, this);
  return true;
}

//------------------------------------------------------------------------------
// Stop capturing
//------------------------------------------------------------------------------
void CameraStream::Stop()
{
  if (!pw_loop_) {
    // Not running
    return;
  }
  // Stop the main loop
  pw_main_loop_quit(pw_loop_);
  if (pipewire_thread_.joinable()) {
    pipewire_thread_.join();
  }

  // Cleanup
  if (pw_stream_) {
    pw_stream_destroy(pw_stream_);
    pw_stream_ = nullptr;
  }
  if (pw_core_) {
    pw_core_disconnect(pw_core_);
    pw_core_ = nullptr;
  }
  if (pw_context_) {
    pw_context_destroy(pw_context_);
    pw_context_ = nullptr;
  }
  if (pw_loop_) {
    pw_main_loop_destroy(pw_loop_);
    pw_loop_ = nullptr;
  }
  pw_deinit();
}

//------------------------------------------------------------------------------
// Private method: run the main loop
//------------------------------------------------------------------------------
void CameraStream::RunPipeWireLoop()
{
  pw_main_loop_run(pw_loop_);
}

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


//------------------------------------------------------------------------------
// Private method: called each time there's a new MJPEG frame
//------------------------------------------------------------------------------
void CameraStream::HandleProcess()
{
  if (!pw_stream_) return;
  pw_buffer* buf = pw_stream_dequeue_buffer(pw_stream_);
  if (!buf) return;

  if (!buf->buffer->datas[0].data) {
    pw_stream_queue_buffer(pw_stream_, buf);
    return;
  }

  auto*    compressedData = static_cast<uint8_t*>(buf->buffer->datas[0].data);
  size_t   compressedSize = buf->buffer->datas[0].chunk->size;

  int ret = decode_mjpeg(compressedData, compressedSize,
                         decoded_buffer_.get(), width_, height_);
  if (ret == 0) {
    {
      std::lock_guard<std::mutex> lock(frame_mutex_);
      new_frame_available_ = true;
      //std::cout << "new_frame_available_ = true"<<std::endl;

      //save_image_to_jpeg("/home/tcna/Pictures/test.jpeg", decoded_buffer_.get(), width_, height_, 3, 90);

      SPDLOG_TRACE("[camera_plugin] Texture::blit_fb");
      registrar_->texture_registrar()->TextureMakeCurrent();
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
      glViewport(0, 0, width_, height_);

      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texture_id_);
      glUniform1i(0, 0);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR);

      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width_, height_, 0,
                   GL_RGB, GL_UNSIGNED_BYTE, decoded_buffer_.get());
      glGenerateMipmap(GL_TEXTURE_2D);

      glBindFramebuffer(GL_FRAMEBUFFER, 0);

    }
    // Tell Flutter there's a new frame
    //if (registrar_->texture_registrar()) {
      registrar_->texture_registrar()->TextureClearCurrent();
      registrar_->texture_registrar()->MarkTextureFrameAvailable(texture_id_);
    //}
    glFinish();
  } else {
    std::fprintf(stderr, "[CameraStream::HandleProcess] MJPEG decode failed.\n");
  }

  pw_stream_queue_buffer(pw_stream_, buf);
}

//------------------------------------------------------------------------------
// Static callback proxies
//------------------------------------------------------------------------------
void CameraStream::OnStreamStateChanged(void *data,
                                        pw_stream_state old_state,
                                        pw_stream_state new_state,
                                        const char *error)
{
  // For this example, just log
  std::fprintf(stderr, "[CameraStream] state changed from %d to %d (%s)\n",
               old_state, new_state, (error ? error : "no error"));
}

void CameraStream::OnStreamParamChanged(void *data,
                                        uint32_t id,
                                        const spa_pod* param)
{
  std::fprintf(stderr, "[CameraStream] OnStreamParamChanged: id=%u\n", id);
}

void CameraStream::OnStreamProcess(void *data)
{
  auto *self = static_cast<CameraStream*>(data);
  self->HandleProcess();
}
