#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <flutter/binary_messenger.h>
#include <flutter/encodable_value.h>
#include <flutter/event_channel.h>
#include <flutter/event_sink.h>

extern "C" {
#include <gst/gst.h>
}

using namespace flutter;

class AudioPlayer {
 public:
  AudioPlayer(const std::string& playerId, BinaryMessenger* messenger);

  ~AudioPlayer();

  std::optional<int64_t> GetPosition();

  std::optional<int64_t> GetDuration();

  [[nodiscard]] bool GetLooping() const;

  void Play();

  void Pause();

  void Stop();

  void Resume();

  void Dispose();

  void SetBalance(float balance);

  void SetLooping(bool isLooping);

  void SetVolume(double volume) const;

  void SetPlaybackRate(double rate);

  void SetPosition(int64_t position);

  void SetSourceUrl(const std::string& url);

  void SetSourceBytes(const std::vector<uint8_t>& bytes);

  void ReleaseMediaSource();

  void OnError(const gchar* code,
               const gchar* message,
               EncodableValue* details,
               GError** error);

  void OnLog(const gchar* message);

 private:
  const std::string eventChannelName_;
  GMainContext* context_;
  GstState media_state_;

  // Gst members
  GstElement* playbin_{};
  GstElement* source_{};
  GstElement* panorama_{};
  GstElement* audiobin_{};
  GstElement* audiosink_{};
  GstPad* panoramaSinkPad_{};
  GstBus* bus_{};

  bool isInitialized_{};
  bool isPlaying_{};
  bool isLooping_{};
  bool isSeekCompleted_ = true;
  double playbackRate_ = 1.0;

  std::string url_;
  std::string byte_source_path_;

  std::unique_ptr<flutter::EventChannel<>> event_channel_;
  std::mutex event_sink_mutex_;
  std::unique_ptr<flutter::EventSink<>> event_sink_;

  // Cached duration discovered out-of-band via GstDiscoverer. Populated when
  // SetSourceUrl runs, since gst_element_query_duration on playbin returns
  // wrong values for variable-bitrate MP3s. -1 means "no value, fall back to
  // the playbin query".
  std::atomic<int64_t> discovered_duration_ms_{-1};

  void SendEvent(const EncodableValue& value);

  void StartDurationDiscovery(const std::string& uri);

  void CleanupByteSource();

  static void SourceSetup(GstElement* playbin,
                          GstElement* source,
                          GstElement** p_src);

  static void AboutToFinish(GstElement* playbin, AudioPlayer* self);

  static gboolean OnBusMessage(GstBus* bus,
                               GstMessage* message,
                               AudioPlayer* data);

  void SetPlayback(int64_t seekTo, double rate);

  void OnMediaError(GError* error, gchar* debug);

  void OnMediaStateChange(const GstObject* src,
                          const GstState* old_state,
                          const GstState* new_state);

  void OnDurationUpdate();

  void OnSeekCompleted();

  void OnPlaybackEnded();

  void OnPrepared(bool isPrepared);
};
