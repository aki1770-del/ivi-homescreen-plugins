
#include "audio_player.h"

#include <flutter/event_stream_handler_functions.h>
#include <flutter/standard_method_codec.h>
#include <spdlog/spdlog.h>

extern "C" {
#include <gst/pbutils/gstdiscoverer.h>
}

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <unistd.h>

#define STR_LINK_TROUBLESHOOTING \
  "https://github.com/bluefireteam/audioplayers/blob/main/troubleshooting.md"

AudioPlayer::AudioPlayer(const std::string& playerId,
                         BinaryMessenger* messenger)
    : eventChannelName_(playerId),
      media_state_(GST_STATE_VOID_PENDING) {
  event_channel_ = std::make_unique<flutter::EventChannel<>>(
      messenger, eventChannelName_,
      &flutter::StandardMethodCodec::GetInstance());
  event_channel_->SetStreamHandler(
      std::make_unique<flutter::StreamHandlerFunctions<>>(
          [this](const EncodableValue* /* arguments */,
                 std::unique_ptr<flutter::EventSink<>>&& events)
              -> std::unique_ptr<flutter::StreamHandlerError<>> {
            std::lock_guard<std::mutex> lock(event_sink_mutex_);
            event_sink_ = std::move(events);
            return nullptr;
          },
          [this](const EncodableValue* /* arguments */)
              -> std::unique_ptr<flutter::StreamHandlerError<>> {
            std::lock_guard<std::mutex> lock(event_sink_mutex_);
            event_sink_.reset();
            return nullptr;
          }));

  // Get the calling context.
  context_ = g_main_context_get_thread_default();

  playbin_ = gst_element_factory_make("playbin", nullptr);
  if (!playbin_) {
    throw std::runtime_error("Not all elements could be created.");
  }

  // Setup stereo balance controller
  panorama_ = gst_element_factory_make("audiopanorama", nullptr);
  if (panorama_) {
    audiobin_ = gst_bin_new(nullptr);
    audiosink_ = gst_element_factory_make("autoaudiosink", nullptr);

    gst_bin_add_many(GST_BIN(audiobin_), panorama_, audiosink_, nullptr);
    gst_element_link(panorama_, audiosink_);

    GstPad* sinkpad = gst_element_get_static_pad(panorama_, "sink");
    panoramaSinkPad_ = gst_ghost_pad_new("sink", sinkpad);
    gst_element_add_pad(audiobin_, panoramaSinkPad_);
    gst_object_unref(GST_OBJECT(sinkpad));

    g_object_set(G_OBJECT(playbin_), "audio-sink", audiobin_, nullptr);
    g_object_set(G_OBJECT(panorama_), "method", 1, nullptr);
  }

  // Setup source options
  g_signal_connect(playbin_, "source-setup",
                   G_CALLBACK(AudioPlayer::SourceSetup), &source_);

  // playbin fires about-to-finish when the current source is draining.
  // This is more reliable than waiting for GST_MESSAGE_EOS (which we have
  // observed to not fire in some pipeline configurations).
  g_signal_connect(playbin_, "about-to-finish",
                   G_CALLBACK(AudioPlayer::AboutToFinish), this);

  bus_ = gst_element_get_bus(playbin_);

  // Watch bus messages for one time events
  gst_bus_add_watch(bus_, reinterpret_cast<GstBusFunc>(OnBusMessage), this);
}

AudioPlayer::~AudioPlayer() {
  if (event_channel_) {
    event_channel_->SetStreamHandler(nullptr);
  }
  if (playbin_) {
    gst_element_set_state(playbin_, GST_STATE_NULL);
  }
}

void AudioPlayer::SendEvent(const EncodableValue& value) {
  // EventSink::Success internally needs the Flutter platform thread to
  // deliver the message. Calling it from arbitrary threads (GStreamer bus
  // thread, streaming thread inside about-to-finish, or the platform thread
  // itself while it is in another callback) deadlocks. Marshal every
  // emission to the GLib main loop so exactly one thread ever drives the
  // sink, and never hold the sink mutex while calling Success().
  struct Ctx {
    AudioPlayer* self;
    EncodableValue* value;
  };
  auto* ctx = new Ctx{this, new EncodableValue(value)};
  g_idle_add_full(
      G_PRIORITY_DEFAULT,
      [](gpointer user_data) -> gboolean {
        auto* c = static_cast<Ctx*>(user_data);
        flutter::EventSink<>* sink;
        {
          std::lock_guard<std::mutex> lock(c->self->event_sink_mutex_);
          sink = c->self->event_sink_.get();
        }
        if (sink) {
          sink->Success(*c->value);
        }
        delete c->value;
        delete c;
        return G_SOURCE_REMOVE;
      },
      ctx, nullptr);
}

void AudioPlayer::SourceSetup(GstElement* /* playbin */,
                              GstElement* /* source */,
                              GstElement** /* p_src */) {
  // No source-level overrides. The previous implementation set
  // ssl-strict=FALSE on souphttpsrc, which disabled certificate verification
  // on every HTTPS source — a MITM hole. GStreamer's defaults are correct.
}

void AudioPlayer::AboutToFinish(GstElement* /* playbin */, AudioPlayer* self) {
  // Fires on the playbin streaming thread. Do not call Stop()/Pause() here —
  // they take GStreamer state-change locks and deadlock the thread driving
  // the pipeline. Just emit the completion event so Dart's state machine
  // advances; SendEvent already marshals to the main loop.
  if (!self->isPlaying_) {
    return;
  }
  self->isPlaying_ = false;
  const EncodableValue value(EncodableMap{
      {EncodableValue("event"), EncodableValue("audio.onComplete")},
      {EncodableValue("value"), flutter::EncodableValue(true)},
  });
  self->SendEvent(value);
}

// Returns true if `url` starts with one of the schemes we permit playbin to
// consume. Anything else (rtsp://, smb://, gst-pipeline://, etc.) lets the
// caller drive arbitrary GStreamer source elements, which is too much
// authority for a plugin that takes URLs from Dart.
static bool IsAllowedSourceUrl(const std::string& url) {
  static constexpr std::string_view kAllowedSchemes[] = {
      "file://", "http://", "https://", "data:",
  };
  for (const auto& scheme : kAllowedSchemes) {
    if (url.compare(0, scheme.size(), scheme) == 0) {
      return true;
    }
  }
  return false;
}

void AudioPlayer::SetSourceUrl(const std::string& url) {
  if (!url.empty() && !IsAllowedSourceUrl(url)) {
    spdlog::warn("[audioplayers] rejecting setSourceUrl with disallowed scheme "
                 "channel={} url={}",
                 eventChannelName_, url);
    OnError("LinuxAudioError",
            "URL scheme not permitted (allowed: file, http, https, data).",
            nullptr, nullptr);
    return;
  }
  // Always reset: Dart's setSource contract is "prepare this source from
  // scratch". A "same URL → no-op" shortcut would leave the pipeline wherever
  // the last playback left it (typically at EOS), so a subsequent resume of
  // the same clip would produce no audio.
  url_ = url;
  gst_element_set_state(playbin_, GST_STATE_NULL);
  isInitialized_ = false;
  isPlaying_ = false;
  discovered_duration_ms_.store(-1, std::memory_order_relaxed);
  if (url_.empty()) {
    return;
  }
  g_object_set(GST_OBJECT(playbin_), "uri", url_.c_str(), NULL);
  StartDurationDiscovery(url_);
  const GstStateChangeReturn ret =
      gst_element_set_state(playbin_, GST_STATE_READY);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    OnError("LinuxAudioError",
            "Unable to set the pipeline to GST_STATE_READY.", nullptr, nullptr);
  }
}

void AudioPlayer::SetSourceBytes(const std::vector<uint8_t>& bytes) {
  CleanupByteSource();

  const char* tmpdir = std::getenv("XDG_RUNTIME_DIR");
  if (!tmpdir || !*tmpdir) {
    tmpdir = "/tmp";
  }
  std::string path_template =
      std::string(tmpdir) + "/audioplayers_linux_XXXXXX";
  std::vector<char> mutable_template(path_template.begin(), path_template.end());
  mutable_template.push_back('\0');
  const int fd = mkstemp(mutable_template.data());
  if (fd < 0) {
    OnError("LinuxAudioError",
            "Failed to create temp file for setSourceBytes.", nullptr, nullptr);
    return;
  }

  const char* data = reinterpret_cast<const char*>(bytes.data());
  size_t remaining = bytes.size();
  while (remaining > 0) {
    const ssize_t n = write(fd, data, remaining);
    if (n < 0) {
      close(fd);
      unlink(mutable_template.data());
      OnError("LinuxAudioError",
              "Failed to write bytes for setSourceBytes.", nullptr, nullptr);
      return;
    }
    data += n;
    remaining -= static_cast<size_t>(n);
  }
  close(fd);

  byte_source_path_ = mutable_template.data();
  SetSourceUrl(std::string("file://") + byte_source_path_);
}

void AudioPlayer::CleanupByteSource() {
  if (!byte_source_path_.empty()) {
    unlink(byte_source_path_.c_str());
    byte_source_path_.clear();
  }
}

void AudioPlayer::ReleaseMediaSource() {
  if (isPlaying_)
    isPlaying_ = false;
  if (isInitialized_)
    isInitialized_ = false;
  url_.clear();

  // Bounded wait: the original GST_CLOCK_TIME_NONE could hang the caller
  // forever if the pipeline got wedged.
  GstState playbinState;
  gst_element_get_state(playbin_, &playbinState, nullptr, 2 * GST_SECOND);
  if (playbinState > GST_STATE_NULL) {
    gst_element_set_state(playbin_, GST_STATE_NULL);
  }
}

gboolean AudioPlayer::OnBusMessage(GstBus* /* bus */,
                                   GstMessage* message,
                                   AudioPlayer* data) {
  switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
      GError* err;
      gchar* debug;
      gst_message_parse_error(message, &err, &debug);
      spdlog::error(
          "[audioplayers] gst error channel={} domain={} code={} message={} "
          "debug={}",
          data->eventChannelName_, g_quark_to_string(err->domain), err->code,
          err->message ? err->message : "", debug ? debug : "");
      data->OnMediaError(err, debug);
      g_error_free(err);
      g_free(debug);
      break;
    }
    case GST_MESSAGE_WARNING: {
      GError* err;
      gchar* debug;
      gst_message_parse_warning(message, &err, &debug);
      spdlog::warn(
          "[audioplayers] gst warning channel={} domain={} code={} message={} "
          "debug={}",
          data->eventChannelName_, g_quark_to_string(err->domain), err->code,
          err->message ? err->message : "", debug ? debug : "");
      g_error_free(err);
      g_free(debug);
      break;
    }
    case GST_MESSAGE_NEW_CLOCK:
      if (GST_MESSAGE_SRC(message) == GST_OBJECT(data->playbin_)) {
        data->OnDurationUpdate();
      }
      break;
    case GST_MESSAGE_STATE_CHANGED: {
      GstState old_state, new_state;
      gst_message_parse_state_changed(message, &old_state, &new_state, nullptr);
      data->OnMediaStateChange(GST_MESSAGE_SRC(message), &old_state,
                               &new_state);
      break;
    }
    case GST_MESSAGE_EOS:
      if (GST_MESSAGE_SRC(message) == GST_OBJECT(data->playbin_) &&
          data->isPlaying_) {
        data->OnPlaybackEnded();
      }
      break;
    case GST_MESSAGE_DURATION_CHANGED:
      data->OnDurationUpdate();
      break;
    case GST_MESSAGE_ASYNC_DONE:
      if (GST_MESSAGE_SRC(message) == GST_OBJECT(data->playbin_)) {
        if (!data->isSeekCompleted_) {
          data->OnSeekCompleted();
          data->isSeekCompleted_ = true;
        }
      }
      break;
    default:
      // For more GstMessage types see:
      // https://gstreamer.freedesktop.org/documentation/gstreamer/gstmessage.html?gi-language=c#enumerations
      break;
  }

  // Continue watching for messages
  return TRUE;
}

void AudioPlayer::OnMediaError(GError* error, gchar* /* debug */) {
  const auto code = "LinuxAudioError";
  gchar const* message;
  const auto details_str = std::string(error->message) + " (Domain: " +
                           std::string(g_quark_to_string(error->domain)) +
                           ", Code: " + std::to_string(error->code) + ")";
  EncodableValue details(details_str.c_str());
  // https://gstreamer.freedesktop.org/documentation/gstreamer/gsterror.html#enumerations
  if (error->domain == GST_STREAM_ERROR) {
    message =
        "Failed to set source. For troubleshooting, "
        "see: " STR_LINK_TROUBLESHOOTING;
  } else {
    message = "Unknown GstGError. See details.";
  }
  OnError(code, message, &details, &error);
}

void AudioPlayer::OnError(const gchar* code,
                          const gchar* message,
                          EncodableValue* /* details */,
                          GError** /* error */) {
  const EncodableValue value(
      EncodableMap{{EncodableValue("code"), EncodableValue(code)},
                   {EncodableValue("message"), EncodableValue(message)}});
  SendEvent(value);
}

void AudioPlayer::OnMediaStateChange(const GstObject* src,
                                     const GstState* old_state,
                                     const GstState* new_state) {
  media_state_ = *new_state;

  if (!playbin_) {
    OnError("LinuxAudioError",
            "Player was already disposed (OnMediaStateChange).", nullptr,
            nullptr);
    return;
  }

  if (src == GST_OBJECT(playbin_)) {
    if (*new_state == GST_STATE_READY) {
      // Need to set to pause state, in order to make player functional
      const GstStateChangeReturn ret =
          gst_element_set_state(playbin_, GST_STATE_PAUSED);
      if (ret == GST_STATE_CHANGE_FAILURE) {
        const auto error_description =
            "Unable to set the pipeline from GST_STATE_READY to "
            "GST_STATE_PAUSED.";
        if (isInitialized_) {
          OnError("LinuxAudioError", error_description, nullptr, nullptr);
        } else {
          EncodableValue details(error_description);
          OnError("LinuxAudioError",
                  "Failed to set source. For troubleshooting, "
                  "see: " STR_LINK_TROUBLESHOOTING,
                  &details, nullptr);
        }
      }
      if (isInitialized_) {
        isInitialized_ = false;
      }
    } else if (*old_state == GST_STATE_PAUSED &&
               *new_state == GST_STATE_PLAYING) {
      OnDurationUpdate();
    } else if (*new_state >= GST_STATE_PAUSED) {
      if (!isInitialized_) {
        isInitialized_ = true;
        OnPrepared(true);
        if (isPlaying_) {
          Resume();
        }
      }
    } else if (isInitialized_) {
      isInitialized_ = false;
    }
  }
}

void AudioPlayer::OnPrepared(bool isPrepared) {
  const EncodableValue value(EncodableMap{
      {EncodableValue("event"), EncodableValue("audio.onPrepared")},
      {EncodableValue("value"), flutter::EncodableValue(isPrepared)},
  });
  SendEvent(value);
}

void AudioPlayer::OnDurationUpdate() {
  const EncodableValue value(EncodableMap{
      {EncodableValue("event"), EncodableValue("audio.onDuration")},
      {EncodableValue("value"),
       flutter::EncodableValue(GetDuration().value_or(0))},
  });
  SendEvent(value);
}

void AudioPlayer::OnSeekCompleted() {
  const EncodableValue value(EncodableMap{
      {EncodableValue("event"), EncodableValue("audio.onSeekComplete")},
      {EncodableValue("value"), flutter::EncodableValue(true)},
  });
  SendEvent(value);
}

void AudioPlayer::OnPlaybackEnded() {
  const EncodableValue value(EncodableMap{
      {EncodableValue("event"), EncodableValue("audio.onComplete")},
      {EncodableValue("value"), flutter::EncodableValue(true)},
  });
  SendEvent(value);

  if (GetLooping()) {
    Play();
  } else {
    Stop();
  }
}

void AudioPlayer::OnLog(const gchar* message) {
  const EncodableValue value(EncodableMap{
      {EncodableValue("event"), EncodableValue("audio.onLog")},
      {EncodableValue("value"), flutter::EncodableValue(std::string(message))},
  });
  SendEvent(value);
}

void AudioPlayer::SetBalance(float balance) {
  if (!panorama_) {
    OnLog("Audiopanorama was not initialized");
    return;
  }

  if (balance > 1.0f) {
    balance = 1.0f;
  } else if (balance < -1.0f) {
    balance = -1.0f;
  }
  g_object_set(G_OBJECT(panorama_), "panorama", balance, NULL);
}

void AudioPlayer::SetLooping(const bool isLooping) {
  isLooping_ = isLooping;
}

bool AudioPlayer::GetLooping() const {
  return isLooping_;
}

void AudioPlayer::SetVolume(double volume) const {
  if (volume > 1) {
    volume = 1;
  } else if (volume < 0) {
    volume = 0;
  }
  g_object_set(G_OBJECT(playbin_), "volume", volume, NULL);
}

/**
 * A rate of 1.0 means normal playback rate, 2.0 means double speed.
 * Negative values means backwards playback.
 * A value of 0.0 will pause the player.
 *
 * @param seekTo the position in milliseconds
 * @param rate the playback rate (speed)
 */
void AudioPlayer::SetPlayback(const int64_t seekTo, const double rate) {
  if (rate != 0 && playbackRate_ != rate) {
    playbackRate_ = rate;
  }

  if (!isInitialized_) {
    return;
  }
  // See:
  // https://gstreamer.freedesktop.org/documentation/tutorials/basic/playback-speed.html?gi-language=c
  if (!isSeekCompleted_) {
    return;
  }
  if (rate == 0) {
    // Do not set rate if it's 0, rather pause.
    Pause();
    return;
  }

  isSeekCompleted_ = false;

  GstEvent* seek_event;
  if (rate > 0) {
    seek_event = gst_event_new_seek(
        rate, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
        GST_SEEK_TYPE_SET, seekTo * GST_MSECOND, GST_SEEK_TYPE_NONE, -1);
  } else {
    seek_event = gst_event_new_seek(
        rate, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
        GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, seekTo * GST_MSECOND);
  }

  if (!gst_element_send_event(playbin_, seek_event)) {
    OnLog((std::string("Could not set playback to position ") +
           std::to_string(seekTo) + std::string(" and rate ") +
           std::to_string(rate) + std::string("."))
              .c_str());
    isSeekCompleted_ = true;
  }
}

void AudioPlayer::SetPlaybackRate(const double rate) {
  SetPlayback(GetPosition().value_or(0), rate);
}

/**
 * @param position the position in milliseconds
 */
void AudioPlayer::SetPosition(const int64_t position) {
  if (!isInitialized_) {
    return;
  }
  SetPlayback(position, playbackRate_);
}

/**
 * @return int64_t the position in milliseconds
 */
std::optional<int64_t> AudioPlayer::GetPosition() {
  gint64 current = 0;
  if (!gst_element_query_position(playbin_, GST_FORMAT_TIME, &current)) {
    OnLog("Could not query current position.");
    return std::nullopt;
  }
  return std::make_optional(current / 1000000);
}

/**
 * @return int64_t the duration in milliseconds
 */
std::optional<int64_t> AudioPlayer::GetDuration() {
  // Prefer the GstDiscoverer result when available — gst_element_query_duration
  // is unreliable for variable-bitrate MP3s (the original FIXME).
  const int64_t discovered =
      discovered_duration_ms_.load(std::memory_order_relaxed);
  if (discovered >= 0) {
    return std::make_optional(discovered);
  }
  gint64 duration = 0;
  if (!gst_element_query_duration(playbin_, GST_FORMAT_TIME, &duration)) {
    OnLog("Could not query current duration.");
    return std::nullopt;
  }
  return std::make_optional(duration / 1000000);
}

void AudioPlayer::StartDurationDiscovery(const std::string& uri) {
  // GstDiscoverer runs a brief synchronous probe (5s timeout) on a worker
  // thread so we don't stall the calling thread. Store the result back on the
  // AudioPlayer for GetDuration() to consume. We intentionally use the sync
  // API on a detached thread rather than the async signal-based one to avoid
  // wiring another GLib source into our main loop.
  struct DiscoverCtx {
    std::string uri;
    std::atomic<int64_t>* sink;
  };
  auto* ctx = new DiscoverCtx{uri, &discovered_duration_ms_};
  std::thread([ctx]() {
    GError* err = nullptr;
    GstDiscoverer* discoverer = gst_discoverer_new(5 * GST_SECOND, &err);
    if (!discoverer) {
      spdlog::warn("[audioplayers] gst_discoverer_new failed: {}",
                   err ? err->message : "unknown");
      if (err) g_error_free(err);
      delete ctx;
      return;
    }
    GstDiscovererInfo* info =
        gst_discoverer_discover_uri(discoverer, ctx->uri.c_str(), &err);
    if (info) {
      const GstClockTime dur = gst_discoverer_info_get_duration(info);
      if (GST_CLOCK_TIME_IS_VALID(dur)) {
        ctx->sink->store(static_cast<int64_t>(dur / GST_MSECOND),
                         std::memory_order_relaxed);
      }
      gst_discoverer_info_unref(info);
    } else if (err) {
      spdlog::warn("[audioplayers] gst_discoverer probe failed for {}: {}",
                   ctx->uri, err->message);
      g_error_free(err);
    }
    g_object_unref(discoverer);
    delete ctx;
  }).detach();
}

void AudioPlayer::Play() {
  SetPosition(0);
  Resume();
}

void AudioPlayer::Pause() {
  if (isPlaying_) {
    isPlaying_ = false;
  }
  if (!isInitialized_) {
    return;
  }
  const GstStateChangeReturn ret =
      gst_element_set_state(playbin_, GST_STATE_PAUSED);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    OnError("LinuxAudioError",
            "Unable to set the pipeline to GST_STATE_PAUSED.", nullptr,
            nullptr);
  }
}

void AudioPlayer::Stop() {
  Pause();
  if (!isInitialized_) {
    return;
  }
  SetPosition(0);
  // Wait for the seek/state-change to settle. Bounded so a wedged element
  // can't hang the calling thread (GST_CLOCK_TIME_NONE would).
  constexpr GstClockTime kStopTimeout = 2 * GST_SECOND;
  const GstStateChangeReturn ret =
      gst_element_get_state(playbin_, nullptr, nullptr, kStopTimeout);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    OnError("LinuxAudioError",
            "Unable to seek playback to '0' while stopping the player.",
            nullptr, nullptr);
  } else if (ret == GST_STATE_CHANGE_ASYNC) {
    spdlog::warn(
        "[audioplayers] Stop timed out after 2s waiting for state settle "
        "channel={}",
        eventChannelName_);
  }
}

void AudioPlayer::Resume() {
  if (!isPlaying_) {
    isPlaying_ = true;
  }
  if (!isInitialized_) {
    return;
  }
  const GstStateChangeReturn ret =
      gst_element_set_state(playbin_, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_SUCCESS) {
    // Update duration when start playing, as no event is emitted elsewhere
    OnDurationUpdate();
  } else if (ret == GST_STATE_CHANGE_FAILURE) {
    OnError("LinuxAudioError",
            "Unable to set the pipeline to GST_STATE_PLAYING.", nullptr,
            nullptr);
  }
}

void AudioPlayer::Dispose() {
  if (!playbin_) {
    spdlog::warn("[audioplayers] Dispose() called on already-disposed player "
                 "channel={}",
                 eventChannelName_);
    return;
  }

  ReleaseMediaSource();
  CleanupByteSource();

  if (bus_) {
    gst_bus_remove_watch(bus_);
    gst_object_unref(GST_OBJECT(bus_));
    bus_ = nullptr;
  }

  if (source_) {
    gst_object_unref(GST_OBJECT(source_));
    source_ = nullptr;
  }

  if (panorama_) {
    gst_element_set_state(audiobin_, GST_STATE_NULL);

    gst_element_remove_pad(audiobin_, panoramaSinkPad_);
    gst_bin_remove(GST_BIN(audiobin_), audiosink_);
    gst_bin_remove(GST_BIN(audiobin_), panorama_);

    // audiobin gets unreferenced (2x) via playbin
    panorama_ = nullptr;
  }

  gst_object_unref(GST_OBJECT(playbin_));
  playbin_ = nullptr;
}
