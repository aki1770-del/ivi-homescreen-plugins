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

#include "video_player.h"

#include <flutter/event_channel.h>
#include <flutter/event_stream_handler.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/plugin_registrar_homescreen.h>
#include <flutter/standard_method_codec.h>

#include <climits>
#include <cstring>

#include <backend/backend.h>
#include <plugins/common/common.h>
#include <utility>

#define GSTREAMER_DEBUG 0

namespace video_player_linux {

// Serialize access to the shared EGL texture context across all players.
// EGL contexts can only be current on one thread at a time.
static std::mutex g_texture_context_mutex;

typedef enum {
  GST_PLAY_FLAG_AUDIO = 1 << 0,
  GST_PLAY_FLAG_VIDEO = 1 << 1,
  GST_PLAY_FLAG_TEXT = 1 << 2
} GstPlayFlags;

VideoPlayer::VideoPlayer(flutter::PluginRegistrarDesktop* registrar,
                         std::string uri,
                         std::map<std::string, std::string> http_headers,
                         const GLsizei width,
                         const GLsizei height,
                         const gint64 duration)
    : m_registrar(registrar),
      uri_(std::move(uri)),
      http_headers_(std::move(http_headers)),
      width_(width),
      height_(height),
      duration_(duration),
      event_channel_(nullptr) {
  SPDLOG_DEBUG(
      "[VideoPlayer] uri: {}, http_headers: {}, size: {} x {}, duration: {}",
      uri.c_str(), http_headers_.size(), width, height, duration);

  gst_video_info_init(&info_);

  if (width_ <= 0 || height_ <= 0) {
    SPDLOG_ERROR("[VideoPlayer] Invalid dimensions: {}x{}", width_, height_);
    m_valid = false;
    return;
  }

  std::lock_guard buffer_lock(buffer_mutex_);

  /// Setup OpenGL

  {
    std::lock_guard ctx_lock(g_texture_context_mutex);
    m_registrar->texture_registrar()->TextureMakeCurrent();
    // Double-buffer can be enabled via env var for flicker-sensitive cases.
    const bool double_buf =
        std::getenv("VIDEO_PLAYER_DOUBLE_BUFFER") != nullptr;
    shader_ = std::make_unique<nv12::Shader>(width_, height_, double_buf);
    m_texture_id = shader_->textureId;
    m_registrar->texture_registrar()->TextureClearCurrent();
  }

  /// Setup GL Texture 2D

  m_descriptor.struct_size = sizeof(FlutterDesktopGpuSurfaceDescriptor);
  m_descriptor.handle = &m_texture_id;
  m_descriptor.width = static_cast<size_t>(width_);
  m_descriptor.height = static_cast<size_t>(height_);
  m_descriptor.visible_width = static_cast<size_t>(width_);
  m_descriptor.visible_height = static_cast<size_t>(height_);
  m_descriptor.format = kFlutterDesktopPixelFormatRGBA8888;
  m_descriptor.release_callback = [](void* /* release_context */) {};
  m_descriptor.release_context = this;

  gpu_surface_texture_ = std::make_unique<flutter::GpuSurfaceTexture>(
      kFlutterDesktopGpuSurfaceTypeGlTexture2D,
      [&](size_t /* width */,
          size_t /* height */) -> const FlutterDesktopGpuSurfaceDescriptor* {
        return &m_descriptor;
      });

  flutter::TextureVariant texture = *gpu_surface_texture_;
  m_registrar->texture_registrar()->RegisterTexture(&texture);
  SPDLOG_DEBUG("[VideoPlayer] Registered texture_id={}", m_texture_id);

  /// Setup GST Pipeline

  context_ = g_main_context_get_thread_default();

  playbin_ = gst_element_factory_make("playbin", nullptr);
  if (!playbin_) {
    SPDLOG_ERROR("[VideoPlayer] Failed to create playbin element");
    m_valid = false;
    return;
  }
  g_object_set(playbin_, "uri", uri_.c_str(), nullptr);

  if (!http_headers_.empty()) {
    GstStructure* extraHeaders = gst_structure_new_empty("extra-headers");
    for (const auto& [key, value] : http_headers_) {
      gst_structure_set(extraHeaders, key.c_str(), G_TYPE_STRING, value.c_str(),
                        nullptr);
      SPDLOG_DEBUG("extra-header: {}:{}", key, value);
    }
    g_object_set(playbin_, "extra-headers", extraHeaders, nullptr);
    gst_structure_free(extraHeaders);
  }

  // Try audio sinks in order of preference.  autoaudiosink is avoided
  // because it can pick ALSA on systems where ALSA devices don't exist
  // (e.g. WSL2 with PipeWire/PulseAudio over socket), causing the
  // pipeline to fail during preroll.
  {
    GstElement* audio_sink = nullptr;
    for (const char* name : {"pulsesink", "pipewiresink", "autoaudiosink"}) {
      audio_sink = gst_element_factory_make(name, nullptr);
      if (audio_sink) {
        auto ret = gst_element_set_state(audio_sink, GST_STATE_READY);
        if (ret != GST_STATE_CHANGE_FAILURE) {
          gst_element_set_state(audio_sink, GST_STATE_NULL);
          SPDLOG_DEBUG("[VideoPlayer] Using audio sink: {}", name);
          audio_upgraded_ = true;  // real sink found, no hotplug needed
          break;
        }
        SPDLOG_DEBUG("[VideoPlayer] Audio sink {} failed READY", name);
        gst_element_set_state(audio_sink, GST_STATE_NULL);
        gst_object_unref(audio_sink);
        audio_sink = nullptr;
      }
    }
    if (!audio_sink) {
      audio_sink = gst_element_factory_make("fakesink", "fake-audio");
      if (audio_sink) {
        g_object_set(audio_sink, "sync", TRUE, nullptr);
      }
      spdlog::warn("[VideoPlayer] No audio sink available, using fakesink");
    }
    if (audio_sink) {
      g_object_set(playbin_, "audio-sink", audio_sink, nullptr);
    }
  }

  gint flags = 0;
  g_object_get(playbin_, "flags", &flags, nullptr);
  flags |= GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO;
  flags &= ~GST_PLAY_FLAG_TEXT;
  g_object_set(playbin_, "flags", flags, nullptr);

  int connection_speed = 10000;
  if (const char* env = std::getenv("VIDEO_PLAYER_CONNECTION_SPEED")) {
    char* end = nullptr;
    const long val = std::strtol(env, &end, 10);
    if (end != env && val > 0 && val <= INT_MAX) {
      connection_speed = static_cast<int>(val);
    }
  }
  g_object_set(playbin_, "connection-speed", connection_speed, nullptr);
  g_object_set(playbin_, "volume", volume_, nullptr);

  sink_ = gst_element_factory_make("fakesink", nullptr);
  if (!sink_) {
    SPDLOG_ERROR("[VideoPlayer] Failed to create fakesink element");
    m_valid = false;
    return;
  }
  g_object_set(sink_, "sync", TRUE, nullptr);
  g_object_set(sink_, "signal-handoffs", TRUE, nullptr);
  g_object_set(sink_, "can-activate-pull", TRUE, nullptr);
  handoff_handler_id_ = g_signal_connect(
      sink_, "handoff", reinterpret_cast<GCallback>(handoff_handler), this);

  video_convert_ = gst_element_factory_make("videoconvert", nullptr);
  if (!video_convert_) {
    SPDLOG_ERROR("[VideoPlayer] Failed to create videoconvert element");
    m_valid = false;
    return;
  }

  GstCaps* caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING,
                                      "NV12", nullptr);

  video_scale_ = gst_element_factory_make("videoscale", nullptr);
  if (!video_scale_) {
    SPDLOG_ERROR("[VideoPlayer] Failed to create videoscale element");
    m_valid = false;
    return;
  }

  GstCaps* scale =
      gst_caps_new_simple("video/x-raw", "width", G_TYPE_INT, width_, "height",
                          G_TYPE_INT, height_, nullptr);

  pipeline_ = gst_bin_new(nullptr);

  gst_bin_add_many(GST_BIN(pipeline_), video_convert_, video_scale_, sink_,
                   nullptr);

  if (!gst_element_link_filtered(video_convert_, video_scale_, caps)) {
    SPDLOG_ERROR("[VideoPlayer] Failed to link videoconvert with videoscale");
  }
  gst_caps_unref(caps);

  if (!gst_element_link_filtered(video_scale_, sink_, scale)) {
    SPDLOG_ERROR("[VideoPlayer] Failed to link videoscale with fakesink");
  }
  gst_caps_unref(scale);

  // Ghost pad so playbin can link its decoded output into this bin
  GstPad* pad = gst_element_get_static_pad(video_convert_, "sink");
  GstPad* ghost_pad = gst_ghost_pad_new("sink", pad);
  gst_pad_set_active(ghost_pad, TRUE);
  gst_element_add_pad(pipeline_, ghost_pad);
  gst_object_unref(pad);

  g_object_set(playbin_, "video-sink", pipeline_, nullptr);

  bus_ = gst_element_get_bus(playbin_);
  GSource* bus_source = gst_bus_create_watch(bus_);
  // GstBus source dispatch calls the callback with (GstBus*, GstMessage*,
  // gpointer) arguments matching gst_bus_async_signal_func's signature.
  // Cast through void* to avoid -Wcast-function-type-mismatch.
  g_source_set_callback(bus_source,
                        reinterpret_cast<GSourceFunc>(
                            reinterpret_cast<void*>(gst_bus_async_signal_func)),
                        nullptr, nullptr);
  g_source_attach(bus_source, context_);
  g_source_unref(bus_source);
  on_bus_msg_id_ = g_signal_connect(
      bus_, "message", reinterpret_cast<GCallback>(OnBusMessage), this);
}

VideoPlayer::~VideoPlayer() {
  if (m_valid) {
    Dispose();
  }
}

void VideoPlayer::Dispose() {
  SPDLOG_DEBUG("[VideoPlayer] Dispose");
  m_valid = false;
  is_initialized_ = false;
  StopAudioMonitor();

  std::lock_guard buffer_lock(buffer_mutex_);

  if (bus_) {
    g_signal_handler_disconnect(G_OBJECT(bus_), on_bus_msg_id_);
  }
  if (sink_) {
    g_signal_handler_disconnect(G_OBJECT(sink_), handoff_handler_id_);
  }

  if (playbin_) {
    gst_element_set_state(playbin_, GST_STATE_NULL);
  }

  {
    // Ensure no in-flight handoff callback is using the shader
    std::lock_guard gst_lock(gst_mutex_);
    if (shader_) {
      std::lock_guard ctx_lock(g_texture_context_mutex);
      m_registrar->texture_registrar()->TextureMakeCurrent();
      shader_.reset();
      m_registrar->texture_registrar()->TextureClearCurrent();
    }
  }

  if (m_texture_id != 0) {
    SPDLOG_DEBUG("[VideoPlayer] Unregistering texture_id={}", m_texture_id);
    m_registrar->texture_registrar()->UnregisterTexture(m_texture_id);
    m_texture_id = 0;
  }

  if (bus_) {
    gst_object_unref(bus_);
    bus_ = nullptr;
  }

  {
    std::lock_guard event_lock(event_mutex_);
    event_sink_ = nullptr;
  }
  event_channel_ = nullptr;
}

void VideoPlayer::SetLooping(const bool isLooping) {
  SPDLOG_DEBUG("[VideoPlayer] SetLooping: {}", is_looping_.load());
  is_looping_ = isLooping;
}

void VideoPlayer::SetVolume(const double volume) {
  SPDLOG_DEBUG("[VideoPlayer] SetVolume: {}", volume);
  volume_ = volume;
  g_object_set(playbin_, "volume", volume, nullptr);
}

void VideoPlayer::SetPlaybackSpeed(const double playbackSpeed) {
  // Store the desired rate so it can be applied when the pipeline is ready
  pending_rate_ = playbackSpeed;

  // Seek events require the pipeline to be in PAUSED or PLAYING state
  GstState state;
  gst_element_get_state(playbin_, &state, nullptr, 0);
  if (state < GST_STATE_PAUSED) {
    SPDLOG_DEBUG(
        "[VideoPlayer] Deferring playback speed {} until pipeline is ready",
        playbackSpeed);
    return;
  }

  ApplyPlaybackSpeed();
}

void VideoPlayer::Play() {
  target_state_ = GST_STATE_PLAYING;
  const GstStateChangeReturn ret =
      gst_element_set_state(playbin_, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    spdlog::error("[VideoPlayer] Failed to set state GST_STATE_PLAYING.");
  } else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
    is_live_ = TRUE;
    SPDLOG_DEBUG("[VideoPlayer] Pipeline is live");
  }
}

void VideoPlayer::Pause() {
  GstState state;
  gst_element_get_state(playbin_, &state, nullptr, GST_SECOND);
  if (state != GST_STATE_NULL) {
    target_state_ = GST_STATE_PAUSED;
    const GstStateChangeReturn ret =
        gst_element_set_state(playbin_, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
      std::lock_guard event_lock(event_mutex_);
      if (event_sink_) {
        event_sink_->Error("[VideoPlayer] Unable to Pause Transport.");
      }
      return;
    }
    SPDLOG_DEBUG("[VideoPlayer] Transport Paused.");
  }
}

int64_t VideoPlayer::GetPosition() {
  gint64 pos = 0;
  if (gst_element_query_position(playbin_, GST_FORMAT_TIME, &pos)) {
    position_ = pos;
    SPDLOG_TRACE("[VideoPlayer] Position: {}", pos);
  }
  const gint64 current = position_.load();
  return current >= 0 ? current / GST_MSECOND : 0;
}

void VideoPlayer::SendBufferingUpdate() {
  std::lock_guard event_lock(event_mutex_);
  if (!event_sink_) {
    return;
  }
  auto values = flutter::EncodableList();

  GstQuery* query = gst_query_new_buffering(GST_FORMAT_TIME);
  if (gst_element_query(playbin_, query)) {
    const guint n_ranges = gst_query_get_n_buffering_ranges(query);
    for (guint i = 0; i < n_ranges; i++) {
      gint64 start = 0, stop = 0;
      if (gst_query_parse_nth_buffering_range(query, i, &start, &stop)) {
        values.emplace_back(
            std::in_place_type<flutter::EncodableList>,
            flutter::EncodableList{
                flutter::EncodableValue(std::in_place_type<int64_t>,
                                        start / GST_MSECOND),
                flutter::EncodableValue(std::in_place_type<int64_t>,
                                        stop / GST_MSECOND)});
      }
    }
  }
  gst_query_unref(query);

  auto res = flutter::EncodableMap(
      {{flutter::EncodableValue("event"),
        flutter::EncodableValue("bufferingUpdate")},
       {flutter::EncodableValue("values"),
        flutter::EncodableValue(std::in_place_type<flutter::EncodableList>,
                                std::move(values))}});
  event_sink_->Success(flutter::EncodableValue(
      std::in_place_type<flutter::EncodableMap>, std::move(res)));
}

void VideoPlayer::SeekTo(const int64_t seek) {
  const gint64 position = seek * GST_MSECOND;
  SPDLOG_DEBUG("[VideoPlayer] SeekTo: {} -> {}", seek, position);

  if (!gst_element_seek_simple(
          playbin_, GST_FORMAT_TIME,
          static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH |
                                    GST_SEEK_FLAG_KEY_UNIT),
          position)) {
    SPDLOG_ERROR("[VideoPlayer] Seek Failed");
  }
  gint64 pos;
  gst_element_query_position(playbin_, GST_FORMAT_TIME, &pos);
  position_ = pos;
  SPDLOG_DEBUG("[VideoPlayer] SeekTo: {} -> {}", seek, pos);
}

bool VideoPlayer::IsValid() {
  return m_valid;
}

void VideoPlayer::Init(flutter::BinaryMessenger* messenger) {
  if (is_initialized_) {
    return;
  }

  SPDLOG_DEBUG("[VideoPlayer] Init");
  event_channel_ = std::make_unique<flutter::EventChannel<>>(
      messenger,
      std::string("flutter.io/videoPlayer/videoEvents") +
          std::to_string(m_texture_id),
      &flutter::StandardMethodCodec::GetInstance());

  event_channel_->SetStreamHandler(
      std::make_unique<flutter::StreamHandlerFunctions<>>(
          [this](const flutter::EncodableValue* /* arguments */,
                 std::unique_ptr<flutter::EventSink<>>&& events)
              -> std::unique_ptr<flutter::StreamHandlerError<>> {
            std::lock_guard event_lock(event_mutex_);
            event_sink_ = std::move(events);
            return nullptr;
          },
          [this](const flutter::EncodableValue* /* arguments */)
              -> std::unique_ptr<flutter::StreamHandlerError<>> {
            std::lock_guard event_lock(event_mutex_);
            event_sink_ = nullptr;
            return nullptr;
          }));
}

void VideoPlayer::SetBuffering(const bool buffering) {
  std::lock_guard event_lock(event_mutex_);
  if (event_sink_) {
    SPDLOG_DEBUG("[VideoPlayer] SetBuffering: {}", buffering);
    auto res = flutter::EncodableMap(
        {{flutter::EncodableValue("event"),
          flutter::EncodableValue(buffering ? "bufferingStart"
                                            : "bufferingEnd")}});
    event_sink_->Success(flutter::EncodableValue(
        std::in_place_type<flutter::EncodableMap>, std::move(res)));
  }
}

void VideoPlayer::ApplyPlaybackSpeed() {
  if (pending_rate_ == rate_) {
    return;
  }

  const auto playbackSpeed = pending_rate_;
  const gint64 pos = position_.load();
  GstEvent* seek_event;
  if (playbackSpeed > 0) {
    // Forward playback: from current position to end
    seek_event = gst_event_new_seek(
        playbackSpeed, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
        GST_SEEK_TYPE_SET, pos, GST_SEEK_TYPE_END, 0);
  } else {
    // Reverse playback: from beginning to current position
    seek_event = gst_event_new_seek(
        playbackSpeed, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
        GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, pos);
  }

  // Send seek to playbin_ so the segment event propagates to all elements
  // (including audio). Sending to sink_ alone causes "data flow before segment
  // event" warnings on the audio path.
  if (!gst_element_send_event(playbin_, seek_event)) {
    SPDLOG_ERROR("[VideoPlayer] Failed to set playback speed: {}",
                 playbackSpeed);
    return;
  }
  rate_ = playbackSpeed;

  SPDLOG_DEBUG("[VideoPlayer] Playback speed: {}", rate_);
}

gboolean VideoPlayer::OnAudioRecovery(gpointer user_data) {
  auto* self = static_cast<VideoPlayer*>(user_data);
  SPDLOG_DEBUG("[VideoPlayer] Audio recovery: restarting without audio");

  gint flags = 0;
  g_object_get(self->playbin_, "flags", &flags, nullptr);
  flags &= ~GST_PLAY_FLAG_AUDIO;
  g_object_set(self->playbin_, "flags", flags, nullptr);

  self->is_buffering_ = false;
  self->is_initialized_ = false;
  self->sent_initialized_ = false;

  self->is_buffering_ = false;
  self->is_initialized_ = false;
  self->sent_initialized_ = false;

  gst_element_set_state(self->playbin_, GST_STATE_NULL);
  gst_element_set_state(self->playbin_,
                        static_cast<GstState>(self->target_state_.load()));
  return G_SOURCE_REMOVE;
}

void VideoPlayer::StartAudioMonitor() {
  if (udev_mon_)
    return;  // already running

  udev_ = udev_new();
  if (!udev_)
    return;

  udev_mon_ = udev_monitor_new_from_netlink(udev_, "udev");
  if (!udev_mon_) {
    udev_unref(udev_);
    udev_ = nullptr;
    return;
  }

  udev_monitor_filter_add_match_subsystem_devtype(udev_mon_, "sound", nullptr);
  udev_monitor_enable_receiving(udev_mon_);

  int fd = udev_monitor_get_fd(udev_mon_);
  udev_channel_ = g_io_channel_unix_new(fd);
  udev_watch_id_ = g_io_add_watch(udev_channel_, G_IO_IN, OnUdevEvent, this);

  SPDLOG_DEBUG("[VideoPlayer] Audio hotplug monitor started");

  // Check if a PCM device already exists (device may have appeared
  // before the monitor was set up).
  struct udev_enumerate* enumerate = udev_enumerate_new(udev_);
  udev_enumerate_add_match_subsystem(enumerate, "sound");
  udev_enumerate_scan_devices(enumerate);
  struct udev_list_entry* entry;
  udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerate)) {
    const char* path = udev_list_entry_get_name(entry);
    struct udev_device* dev = udev_device_new_from_syspath(udev_, path);
    if (dev) {
      const char* sysname = udev_device_get_sysname(dev);
      if (sysname && strncmp(sysname, "pcmC", 4) == 0) {
        SPDLOG_DEBUG("[VideoPlayer] Audio device already present: {}", sysname);
        udev_device_unref(dev);
        // Schedule upgrade immediately
        GSource* idle = g_idle_source_new();
        g_source_set_callback(idle, OnAudioUpgrade, this, nullptr);
        g_source_attach(idle, context_);
        g_source_unref(idle);
        break;
      }
      udev_device_unref(dev);
    }
  }
  udev_enumerate_unref(enumerate);
}

void VideoPlayer::StopAudioMonitor() {
  if (udev_watch_id_) {
    g_source_remove(udev_watch_id_);
    udev_watch_id_ = 0;
  }
  if (udev_channel_) {
    g_io_channel_unref(udev_channel_);
    udev_channel_ = nullptr;
  }
  if (udev_mon_) {
    udev_monitor_unref(udev_mon_);
    udev_mon_ = nullptr;
  }
  if (udev_) {
    udev_unref(udev_);
    udev_ = nullptr;
  }
}

gboolean VideoPlayer::OnUdevEvent(GIOChannel* /*channel*/,
                                  GIOCondition /*cond*/,
                                  gpointer user_data) {
  auto* self = static_cast<VideoPlayer*>(user_data);
  struct udev_device* dev = udev_monitor_receive_device(self->udev_mon_);
  if (!dev)
    return G_SOURCE_CONTINUE;

  const char* action = udev_device_get_action(dev);
  const char* sysname = udev_device_get_sysname(dev);

  if (action && sysname && strcmp(action, "add") == 0 &&
      strncmp(sysname, "pcmC", 4) == 0 && !self->audio_upgraded_) {
    SPDLOG_DEBUG("[VideoPlayer] Audio device hotplugged: {}", sysname);
    // Schedule the upgrade on an idle callback to avoid reentrancy
    GSource* idle = g_idle_source_new();
    g_source_set_callback(idle, OnAudioUpgrade, self, nullptr);
    g_source_attach(idle, self->context_);
    g_source_unref(idle);
  }

  udev_device_unref(dev);
  return G_SOURCE_CONTINUE;
}

gboolean VideoPlayer::OnAudioUpgrade(gpointer user_data) {
  auto* self = static_cast<VideoPlayer*>(user_data);
  if (self->audio_upgraded_ || !self->m_valid) {
    return G_SOURCE_REMOVE;
  }

  // Try to create a real audio sink.  If it can reach READY, swap it in.
  GstElement* real_sink = gst_element_factory_make("autoaudiosink", nullptr);
  if (!real_sink) {
    spdlog::warn("[VideoPlayer] Audio upgrade: autoaudiosink unavailable");
    return G_SOURCE_REMOVE;
  }

  if (gst_element_set_state(real_sink, GST_STATE_READY) ==
      GST_STATE_CHANGE_FAILURE) {
    gst_element_set_state(real_sink, GST_STATE_NULL);
    gst_object_unref(real_sink);
    spdlog::warn("[VideoPlayer] Audio upgrade: sink not ready yet");
    return G_SOURCE_REMOVE;  // udev will trigger us again on next hotplug
  }
  gst_element_set_state(real_sink, GST_STATE_NULL);

  // Hot-swap: pause playbin, swap audio-sink, resume
  SPDLOG_DEBUG("[VideoPlayer] Audio upgrade: swapping to real audio sink");
  GstState cur_state = GST_STATE_NULL;
  gst_element_get_state(self->playbin_, &cur_state, nullptr, 0);

  gst_element_set_state(self->playbin_, GST_STATE_READY);
  gst_element_get_state(self->playbin_, nullptr, nullptr, 2 * GST_SECOND);

  g_object_set(self->playbin_, "audio-sink", real_sink, nullptr);

  gst_element_set_state(self->playbin_, cur_state);
  self->audio_upgraded_ = true;
  self->StopAudioMonitor();
  SPDLOG_DEBUG("[VideoPlayer] Audio upgrade: done");
  return G_SOURCE_REMOVE;
}

void VideoPlayer::OnPlaybackEnded() {
  if (is_looping_) {
    SPDLOG_DEBUG("[VideoPlayer] Looping: restarting pipeline");
    is_buffering_ = false;
    gst_element_set_state(playbin_, GST_STATE_READY);
    gst_element_set_state(playbin_, GST_STATE_PLAYING);
    return;
  }
  std::lock_guard event_lock(event_mutex_);
  if (event_sink_) {
    SPDLOG_DEBUG("[VideoPlayer] OnPlaybackEnded");
    auto res = flutter::EncodableMap({{flutter::EncodableValue("event"),
                                       flutter::EncodableValue("completed")}});
    event_sink_->Success(flutter::EncodableValue(
        std::in_place_type<flutter::EncodableMap>, std::move(res)));
  }
}

void VideoPlayer::OnMediaInitialized() {}

void VideoPlayer::OnMediaStateChange(const GstState state) {
  if (state == GST_STATE_NULL) {
    SetBuffering(true);
    SendBufferingUpdate();
  } else {
    SetBuffering(false);

    if (state == GST_STATE_PLAYING) {
      SPDLOG_DEBUG("[VideoPlayer] message state changed, start playing {}",
                   m_texture_id);
      audio_recovery_ = false;
      prepare(this);
      is_initialized_ = true;
      ApplyPlaybackSpeed();

      // If audio was initially a fakesink, monitor for real device hotplug.
      if (!audio_upgraded_) {
        StartAudioMonitor();
      }
    } else if (state == GST_STATE_READY) {
      SPDLOG_DEBUG("[VideoPlayer] message state changed, ready {}",
                   m_texture_id);
    }
  }
}

void VideoPlayer::OnMediaError(GstMessage* msg) {
  GError* err;
  gchar* debug_info;
  gst_message_parse_error(msg, &err, &debug_info);
  const std::string error_msg = err->message ? err->message : "Unknown error";
  spdlog::error("[VideoPlayer] Error: {}:{}", GST_OBJECT_NAME(msg->src),
                error_msg);
  if (debug_info) {
    spdlog::error("[VideoPlayer] {}", debug_info);
    g_free(debug_info);
  }
  g_clear_error(&err);

  std::lock_guard event_lock(event_mutex_);
  if (event_sink_) {
    event_sink_->Error("VideoPlayerError", error_msg);
  }
}

void VideoPlayer::OnMediaDurationChange() {
  GstQuery* query = gst_query_new_duration(GST_FORMAT_TIME);
  if (gst_element_query(playbin_, query)) {
    gst_query_parse_duration(query, nullptr, &duration_);
  }
  gst_query_unref(query);
}

void VideoPlayer::SendInitialized() {
  std::lock_guard event_lock(event_mutex_);
  if (!event_sink_) {
    return;
  }
  auto event = flutter::EncodableMap(
      {{flutter::EncodableValue("event"),
        flutter::EncodableValue("initialized")},
       {flutter::EncodableValue("duration"),
        flutter::EncodableValue(
            std::in_place_type<int64_t>,
            static_cast<int64_t>(duration_ / GST_MSECOND))}});

  event.insert({flutter::EncodableValue("width"),
                flutter::EncodableValue(std::in_place_type<int32_t>,
                                        static_cast<int32_t>(width_))});
  event.insert({flutter::EncodableValue("height"),
                flutter::EncodableValue(std::in_place_type<int32_t>,
                                        static_cast<int32_t>(height_))});

  event_sink_->Success(flutter::EncodableValue(
      std::in_place_type<flutter::EncodableMap>, std::move(event)));
}

void VideoPlayer::OnTag(const GstTagList* list,
                        const gchar* tag,
                        gpointer /* user_data */) {
  const std::string tag_str = tag;
  if (const auto type = gst_tag_get_type(tag);
      tag_str == "audio-codec" && type == 64) {
    gchar* value = nullptr;
    if (gst_tag_list_get_string(list, tag, &value) && value) {
      spdlog::debug("[VideoPlayer] audio-codec: {}", value);
      g_free(value);
    }
  } else if (tag_str == "video-codec" && type == 64) {
    gchar* value = nullptr;
    if (gst_tag_list_get_string(list, tag, &value) && value) {
      spdlog::debug("[VideoPlayer] video-codec: {}", value);
      g_free(value);
    }
  } else if (tag_str == "maximum-bitrate" && type == 28) {
    guint value = 0;
    if (gst_tag_list_get_uint(list, tag, &value)) {
      spdlog::debug("[VideoPlayer] maximum-bitrate: {}", value);
    }
  } else if (tag_str == "minimum-bitrate" && type == 28) {
    guint value = 0;
    if (gst_tag_list_get_uint(list, tag, &value)) {
      spdlog::debug("[VideoPlayer] minimum-bitrate: {}", value);
    }
  } else if (tag_str == "bitrate" && type == 28) {
    guint value = 0;
    if (gst_tag_list_get_uint(list, tag, &value)) {
      spdlog::debug("[VideoPlayer] bitrate: {}", value);
    }
  }
}

void VideoPlayer::handoff_handler(GstElement* /* fakesink */,
                                  GstBuffer* buffer,
                                  GstPad* /* pad */,
                                  void* user_data) {
  const auto obj = static_cast<VideoPlayer*>(user_data);
  if (!obj->m_valid || !obj->is_initialized_) {
    return;
  }

  std::lock_guard lock(obj->gst_mutex_);
  if (obj->info_.finfo == nullptr || !obj->shader_) {
    return;
  }
  GstVideoFrame frame;
  if (gst_video_frame_map(&frame, &obj->info_, buffer, GST_MAP_READ)) {
    {
      std::lock_guard ctx_lock(g_texture_context_mutex);
      obj->m_registrar->texture_registrar()->TextureMakeCurrent();
      glBindVertexArray(obj->shader_->vertex_arr_id_);

      if (const guint n_planes = GST_VIDEO_INFO_N_PLANES(&obj->info_);
          n_planes == 2) {
        // Assume NV12
        obj->shader_->load_pixels(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0),
                                  GST_VIDEO_FRAME_PLANE_DATA(&frame, 1),
                                  GST_VIDEO_FRAME_COMP_PSTRIDE(&frame, 0),
                                  GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0),
                                  GST_VIDEO_FRAME_COMP_PSTRIDE(&frame, 1),
                                  GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 1));
      } else {
        // Assume RGB
        obj->shader_->load_rgb_pixels(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
      }
      gst_video_frame_unmap(&frame);

      // Render NV12→RGBA into the render target
      glBindFramebuffer(GL_FRAMEBUFFER, obj->shader_->render_target());
      obj->shader_->draw_core();

      // When double-buffered, blit back→front to avoid tearing
      if (obj->shader_->double_buffer) {
        obj->shader_->blit_to_front();
      }

      obj->m_registrar->texture_registrar()->TextureClearCurrent();
    }

    // Send initialized event after first frame so the Dart Texture widget
    // doesn't display stale GL content before real video arrives.
    if (!obj->sent_initialized_) {
      obj->sent_initialized_ = true;
      obj->SendInitialized();
    }

    obj->m_registrar->texture_registrar()->MarkTextureFrameAvailable(
        obj->m_texture_id);
    SPDLOG_TRACE("[VideoPlayer] frame");
  } else {
    SPDLOG_ERROR("[VideoPlayer] Cannot read video frame out from buffer");
  }
}

gboolean VideoPlayer::OnBusMessage(GstBus* /* bus */,
                                   GstMessage* msg,
                                   void* user_data) {
  auto obj = static_cast<VideoPlayer*>(user_data);
  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR:
      obj->OnMediaError(msg);
      return FALSE;
    case GST_MESSAGE_EOS:
    case GST_MESSAGE_SEGMENT_DONE: {
      SPDLOG_DEBUG(
          "[VideoPlayer] {}: texture_id: {}",
          (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS ? "EOS" : "Segment done"),
          obj->m_texture_id);
      obj->OnPlaybackEnded();
      break;
    }
    case GST_MESSAGE_STATE_CHANGED: {
      GstState old_state, new_state, pending_state;
      gst_message_parse_state_changed(msg, &old_state, &new_state,
                                      &pending_state);
      if (GST_MESSAGE_SRC(msg) == GST_OBJECT(obj->playbin_)) {
        obj->OnMediaStateChange(new_state);
      }
      break;
    }
    case GST_MESSAGE_DURATION_CHANGED: {
      obj->OnMediaDurationChange();
      break;
    }
#if 0
    case GST_MESSAGE_LATENCY: {
      auto src = GST_MESSAGE_SRC(msg);
      SPDLOG_DEBUG("[VideoPlayer] Latency: {}", src->name);
      GstQuery* query;
      gboolean res;
      query = gst_query_new_latency();
      res = gst_element_query(obj->playbin_, query);
      if (res) {
        GstClockTime min_latency;
        GstClockTime max_latency;
        gst_query_parse_latency(query, &obj->is_live_, &min_latency,
                                &max_latency);
      }
      gst_query_unref(query);
      break;
    }
#endif
    case GST_MESSAGE_WARNING: {
      GError* warn_err = nullptr;
      gchar* warn_debug = nullptr;
      gst_message_parse_warning(msg, &warn_err, &warn_debug);
      spdlog::warn("[VideoPlayer] Warning: {}:{} debug={}",
                   GST_OBJECT_NAME(msg->src), warn_err ? warn_err->message : "",
                   warn_debug ? warn_debug : "");
      g_clear_error(&warn_err);
      g_free(warn_debug);
      break;
    }
    case GST_MESSAGE_ASYNC_DONE: {
      SPDLOG_DEBUG("[VideoPlayer] Async Done");
      // bufferingEnd
      break;
    }
    case GST_MESSAGE_NEW_CLOCK: {
      GstClock* clock;
      gst_message_parse_new_clock(msg, &clock);
      const GstClockTime time = gst_clock_get_time(clock);
      (void)time;
      SPDLOG_DEBUG("[VideoPlayer] New Clock: {}", time);
      break;
    }
    case GST_MESSAGE_BUFFERING: {
      // no state management needed for live pipelines
      if (obj->is_live_)
        break;

      gint percent;
      gst_message_parse_buffering(msg, &percent);
      SPDLOG_DEBUG("[VideoPlayer] Buffering: {}% texture_id: {}", percent,
                   obj->m_texture_id);

      obj->SendBufferingUpdate();

      if (percent == 100) {
        // a 100% message means buffering is done
        if (obj->is_buffering_) {
          obj->is_buffering_ = false;
          obj->SetBuffering(false);
        }
        // if the desired state is playing, resume
        if (obj->target_state_ == GST_STATE_PLAYING) {
          gst_element_set_state(obj->playbin_, GST_STATE_PLAYING);
        }
      } else {
        // buffering busy
        if (!obj->is_buffering_ && obj->target_state_ == GST_STATE_PLAYING) {
          // pause the pipeline while buffering
          gst_element_set_state(obj->playbin_, GST_STATE_PAUSED);
        }
        if (!obj->is_buffering_) {
          obj->is_buffering_ = true;
          obj->SetBuffering(true);
        }
      }
      break;
    }
#if GSTREAMER_DEBUG
    case GST_MESSAGE_STREAM_STATUS: {
      GstStreamStatusType type;
      GstElement* owner;
      const GValue* val;
      gchar* path;
      GstTask* task = nullptr;

      SPDLOG_DEBUG("STREAM_STATUS:");
      gst_message_parse_stream_status(msg, &type, &owner);

      val = gst_message_get_stream_status_object(msg);

      SPDLOG_DEBUG("\ttype:   {}", static_cast<guint>(type));
      path = gst_object_get_path_string(GST_MESSAGE_SRC(msg));
      SPDLOG_DEBUG("\tsource: {}", path);
      g_free(path);
      path = gst_object_get_path_string(GST_OBJECT(owner));
      SPDLOG_DEBUG("\towner:  {}", path);
      g_free(path);
      SPDLOG_DEBUG("\tobject: type {}, value {}", G_VALUE_TYPE_NAME(val),
                   g_value_get_object(val));

      /* see if we know how to deal with this object */
      if (G_VALUE_TYPE(val) == GST_TYPE_TASK) {
        task = GST_TASK(g_value_get_object(val));
      }

      switch (type) {
        case GST_STREAM_STATUS_TYPE_CREATE:
          SPDLOG_DEBUG("Created task: {}", fmt::ptr(task));
          break;
        case GST_STREAM_STATUS_TYPE_ENTER:
          SPDLOG_DEBUG("raising task priority");
          // setpriority (PRIO_PROCESS, 0, -10);
          break;
        case GST_STREAM_STATUS_TYPE_LEAVE:
        default:
          break;
      }
      break;
    }
    case GST_MESSAGE_RESET_TIME: {
      GstClockTime running_time;
      gst_message_parse_reset_time(msg, &running_time);
      if (running_time > 0) {
        g_print("reset-time: %" GST_TIME_FORMAT, GST_TIME_ARGS(running_time));
      }
      break;
    }
    // element specific message
    case GST_MESSAGE_ELEMENT: {
      SPDLOG_DEBUG("message-element: {}",
                   gst_structure_get_name(gst_message_get_structure(msg)));
      break;
    }
    case GST_MESSAGE_TAG: {
#if GSTREAMER_DEBUG
      GstTagList* tags = nullptr;
      gst_message_parse_tag(msg, &tags);
      gst_tag_list_foreach(tags, VideoPlayer::OnTag, obj);
      gst_tag_list_unref(tags);
#endif
      break;
    }
#endif
    default:
#if GSTREAMER_DEBUG
    {
      SPDLOG_DEBUG("GST Message Type: {}",
                   gst_message_type_get_name(GST_MESSAGE_TYPE(msg)));
      break;
    }
#else
        ;
#endif
  }
  return TRUE;
}

void VideoPlayer::prepare(VideoPlayer* user_data) {
  SPDLOG_DEBUG("[VideoPlayer] prepare");
  g_object_get(user_data->playbin_, "n-video", &user_data->n_video_, nullptr);
  SPDLOG_DEBUG("[VideoPlayer] {} video streams", user_data->n_video_);
  g_object_get(user_data->playbin_, "current-video", &user_data->current_video_,
               nullptr);
  GstPad* pad = nullptr;
  g_signal_emit_by_name(user_data->playbin_, "get-video-pad",
                        user_data->current_video_, &pad);
  if (!pad) {
    SPDLOG_ERROR(
        "[VideoPlayer] Failed to get video pad, stream number might not exist");
    // TODO g_main_loop_quit(obj->main_loop_);
    return;
  }
  GstCaps* caps = gst_pad_get_current_caps(pad);
  if (!caps) {
    SPDLOG_ERROR("[VideoPlayer] Failed to get caps from video pad");
    gst_object_unref(pad);
    return;
  }
  std::lock_guard lock(user_data->gst_mutex_);
  if (!gst_video_info_from_caps(&user_data->info_, caps)) {
    SPDLOG_ERROR("[VideoPlayer] Fail to get video info from the cap");
  }
  gst_caps_unref(caps);
  SPDLOG_DEBUG("[VideoPlayer] original video width: {}, height: {}",
               user_data->info_.width, user_data->info_.height);
  // set to the target
  if (!gst_video_info_set_format(&user_data->info_, GST_VIDEO_FORMAT_NV12,
                                 static_cast<guint>(user_data->width_),
                                 static_cast<guint>(user_data->height_))) {
    SPDLOG_ERROR("[VideoPlayer] Failed to set the video info to target NV12");
  }
}

}  // namespace video_player_linux
