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

#pragma once

#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <flutter/event_channel.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/plugin_registrar_homescreen.h>

#include "nv12.h"

extern "C" {
#include <gst/gst.h>
#include <gst/video/video.h>
#include <libudev.h>
}

#include "messages.g.h"

class Backend;

namespace video_player_linux {

class VideoPlayer {
 public:
  VideoPlayer(flutter::PluginRegistrarDesktop* registrar,
              std::string uri,
              std::map<std::string, std::string> http_headers,
              GLsizei width,
              GLsizei height,
              gint64 duration);
  ~VideoPlayer();

  void Dispose();
  void SetLooping(bool isLooping);
  void SetVolume(double volume);
  void SetPlaybackSpeed(double playbackSpeed);
  void Play();
  void Pause();
  int64_t GetPosition();
  void SendBufferingUpdate();
  void SeekTo(int64_t seek);
  int64_t GetTextureId() const { return m_texture_id; };
  bool IsValid();

  // Initializes the video player.
  void Init(flutter::BinaryMessenger* messenger);

 private:
  flutter::PluginRegistrarDesktop* m_registrar;
  std::string uri_;
  std::map<std::string, std::string> http_headers_;
  GLsizei width_{};
  GLsizei height_{};
  gint64 duration_{};

  GLuint m_texture_id{};
  std::atomic<bool> m_valid = true;
  std::unique_ptr<flutter::GpuSurfaceTexture> gpu_surface_texture_;

  GMainContext* context_;

  // Gst members
  GstElement* playbin_{};
  GstElement* pipeline_{};
  GstElement* sink_{};
  GstElement* video_convert_{};
  GstElement* video_scale_{};
  GstVideoInfo info_{};
  std::atomic<gint64> position_{0};
  gdouble rate_ = 1.0;
  gdouble pending_rate_ = 1.0;
  GstBus* bus_{};

  gulong handoff_handler_id_;
  gulong on_bus_msg_id_;

  std::atomic<GstState> target_state_{GST_STATE_PAUSED};

  gint n_video_{};
  gint current_video_{};
  std::unique_ptr<nv12::Shader> shader_;
  std::atomic<bool> is_looping_{};
  std::atomic<bool> is_buffering_{};
  gboolean is_live_{};
  double volume_ = 0.0;

  std::mutex gst_mutex_;
  std::mutex event_mutex_;

  std::atomic<bool> audio_recovery_{false};
  std::atomic<bool> audio_upgraded_{false};
  std::atomic<bool> is_initialized_{false};
  std::atomic<bool> sent_initialized_{false};
  void SetBuffering(bool buffering);

  // udev monitor for audio device hotplug
  struct udev* udev_{};
  struct udev_monitor* udev_mon_{};
  GIOChannel* udev_channel_{};
  guint udev_watch_id_{};
  void StartAudioMonitor();
  void StopAudioMonitor();
  static gboolean OnUdevEvent(GIOChannel* channel,
                              GIOCondition cond,
                              gpointer user_data);

  void ApplyPlaybackSpeed();
  void OnPlaybackEnded();
  static gboolean OnAudioRecovery(gpointer user_data);
  static gboolean OnAudioUpgrade(gpointer user_data);
  static void OnMediaInitialized();
  void OnMediaStateChange(GstState state);
  void OnMediaError(GstMessage* msg);
  void OnMediaDurationChange();
  void SendInitialized();

  static void OnTag(const GstTagList* list,
                    const gchar* tag,
                    gpointer user_data);

  // The Surface Descriptor sent to Flutter when a texture frame is available.
  FlutterDesktopGpuSurfaceDescriptor m_descriptor{};

  // A mutex is used to synchronize access to the texture descriptor.
  std::mutex buffer_mutex_;

  // The internal Flutter event channel instance.
  std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>>
      event_channel_;

  // The internal Flutter event sink instance, used to send events to the Dart
  // side.
  std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> event_sink_;

  /**
   * @brief Callback called when fakesink receives new frame data
   * @param[in] fakesink No use
   * @param[in] buffer Pointer to New frame data
   * @param[in] pad No use
   * @param[in,out] user_data Pointer to User data
   * @return void
   * @relation
   * flutter
   */
  static void handoff_handler(GstElement* fakesink,
                              GstBuffer* buffer,
                              GstPad* pad,
                              void* user_data);

  static gboolean OnBusMessage(GstBus* bus, GstMessage* msg, void* user_data);

  /**
   * @brief Prepare
   * @param[in,out] user_data Pointer to User data
   * @return void
   * @relation
   * flutter
   */
  static void prepare(VideoPlayer* user_data);
};
}  // namespace video_player_linux
